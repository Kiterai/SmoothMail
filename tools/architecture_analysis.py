"""Static C++ documentation analysis: Tree-sitter syntax plus Doxygen references.

Not a compiler: overloads and indirect dispatch are conservative approximations.
Qualified identities are retained internally, even when graph labels omit namespaces.
"""

import re
import xml.etree.ElementTree as ET
from collections import defaultdict
from dataclasses import dataclass, field


def walk(node):
    yield node
    for child in node.named_children:
        yield from walk(child)


def text(node):
    return node.text.decode("utf-8") if node is not None else ""


def identifiers(value):
    return re.findall(r"(?:[A-Za-z_]\w*::)*[A-Za-z_]\w*", value)


def declarator_name(node):
    """Find the declared name, not a parameter/template argument name."""
    while node is not None:
        if node.type in ("identifier", "field_identifier", "qualified_identifier",
                         "destructor_name", "operator_name", "type_identifier"):
            return text(node)
        node = node.child_by_field_name("declarator")
    return ""


def function_declarator(node):
    while node is not None:
        if node.type == "function_declarator":
            return node
        node = node.child_by_field_name("declarator")
    return None


@dataclass
class Function:
    identity: str
    name: str
    namespace: str
    owner: str
    source: str
    node: object
    result: set = field(default_factory=set)
    dependencies: set = field(default_factory=set)
    calls: set = field(default_factory=set)


class Analysis:
    def __init__(self, sources):
        try:
            from tree_sitter import Language, Parser
            import tree_sitter_cpp
        except ImportError:
            raise ValueError("Install tools/architecture-requirements.txt to generate architecture documentation") from None
        self.parser = Parser(Language(tree_sitter_cpp.language()))
        self.classes = {}
        self.class_fields = defaultdict(dict)
        self.class_signatures = defaultdict(list)
        self.aliases = {}
        self.functions = []
        self.files = {}
        self.trees = {}
        self.errors = []
        self.originals = sources
        for name, content in sorted(sources.items()):
            # Qt access/metatype macros are not C++ grammar. Preserve byte/line
            # positions while replacing only known declaration/access tokens.
            content = re.sub(rb"\b(Q_OBJECT|Q_GADGET|Q_INVOKABLE|Q_SLOT|Q_SIGNAL|emit|WINAPI)\b",
                             lambda match: b" " * len(match[0]), content)
            content = re.sub(rb"(?m)^[ \t]*Q_DECLARE_[A-Z_]+\([^\r\n]*\)[ \t]*;?[ \t]*",
                             lambda match: b" " * len(match[0]), content)
            content = re.sub(rb"\b(QT_VERSION_STR|__DATE__|__TIME__)\b",
                             lambda match: b'""' + b" " * (len(match[0]) - 2), content)
            # The grammar recovers a missing type in reference defaults '= {}'.
            # This syntax-only placeholder carries no type/call dependency.
            content = re.sub(rb"=\s*\{\}", lambda match: b"=0" + b" " * (len(match[0]) - 2), content)
            content = re.sub(rb"\b(?:public|protected|private)\s+(?:slots|Q_SLOTS)\s*:",
                             lambda match: b"public" + b" " * (len(match[0]) - 7) + b":", content)
            content = re.sub(rb"\b(?:signals|Q_SIGNALS)\s*:",
                             lambda match: b"public" + b" " * (len(match[0]) - 7) + b":", content)
            tree = self.parser.parse(content)
            self.trees[name] = tree
            self.files[name] = {"classes": set(), "functions": set(), "includes": set()}
            self._collect(tree.root_node, name, "", "")
            if tree.root_node.has_error:
                self.errors.append(name)
        self.short_classes = defaultdict(set)
        for name in self.classes:
            self.short_classes[name.rsplit("::", 1)[-1]].add(name)
        self.short_aliases = defaultdict(set)
        for name in self.aliases:
            self.short_aliases[name.rsplit("::", 1)[-1]].add(name)
        self._analyze()

    def _collect(self, node, source, namespace, owner):
        if node.type == "namespace_definition":
            name = text(node.child_by_field_name("name"))
            namespace = "::".join(part for part in (namespace, name) if part)
        if node.type in ("class_specifier", "struct_specifier") and node.child_by_field_name("body") is not None:
            name = text(node.child_by_field_name("name"))
            if name:
                owner = "::".join(part for part in (owner or namespace, name) if part)
                previous = self.classes.get(owner)
                if previous is None or source.endswith(".h"):
                    self.classes[owner] = source
                self.files[source]["classes"].add(owner)
        if node.type == "preproc_include":
            self.files[source]["includes"].add(text(node.child_by_field_name("path")))
        if node.type == "alias_declaration":
            name = text(node.child_by_field_name("name"))
            if name:
                qualified = "::".join(part for part in (owner or namespace, name) if part)
                self.aliases[qualified] = text(node.child_by_field_name("type"))
        if node.type == "function_definition":
            declaration = function_declarator(node.child_by_field_name("declarator"))
            if declaration is not None:
                name = declarator_name(declaration.child_by_field_name("declarator"))
                qualifier, _, simple = name.rpartition("::")
                actual_owner = owner
                if not owner and qualifier:
                    candidates = [qualifier, "::".join(part for part in (namespace, qualifier) if part)]
                    # Headers are collected before analysis; class matching of
                    # out-of-line definitions is finalized in _analyze.
                    actual_owner = candidates[-1]
                qualified = "::".join(part for part in (owner or namespace, name) if part)
                function = Function(f"{source}:{node.start_point.row + 1}:{qualified}", simple or name,
                                    namespace, actual_owner, source, node)
                self.functions.append(function)
                # Class bodies nested inside functions are still visited, but
                # function bodies never create spurious global declarations.
                self._collect_local_classes(node, source, namespace, owner)
                return
        if owner and node.type == "field_declaration":
            declared_type = text(node.child_by_field_name("type"))
            for child in node.children_by_field_name("declarator"):
                declaration = function_declarator(child)
                if declaration is None:
                    self.class_fields[owner][declarator_name(child)] = declared_type
                else:
                    self.class_signatures[owner].append(
                        declared_type + " " + text(declaration.child_by_field_name("parameters")))
        for child in node.named_children:
            self._collect(child, source, namespace, owner)

    def _collect_local_classes(self, node, source, namespace, owner):
        for child in node.named_children:
            if child.type in ("class_specifier", "struct_specifier"):
                self._collect(child, source, namespace, owner)
            else:
                self._collect_local_classes(child, source, namespace, owner)

    def types(self, value, namespace="", source=""):
        result = set()
        queue = [(token, namespace) for token in identifiers(value)]
        seen = set()
        while queue:
            token, context = queue.pop()
            if (token, context) in seen:
                continue
            seen.add((token, context))
            if token in self.classes:
                result.add(token)
                continue
            if token in self.aliases:
                queue.extend((item, token.rpartition("::")[0]) for item in identifiers(self.aliases[token]))
                continue
            scoped = context
            while scoped:
                candidate = scoped + "::" + token
                if candidate in self.classes:
                    result.add(candidate)
                    break
                if candidate in self.aliases:
                    queue.extend((item, candidate.rpartition("::")[0]) for item in identifiers(self.aliases[candidate]))
                    break
                scoped = scoped.rpartition("::")[0]
            else:
                candidates = self.short_classes.get(token, set())
                local = {name for name in candidates if self.classes[name] == source}
                # Do not merge distinct namespaced classes into one vertex.
                result.update(local or (candidates if len(candidates) == 1 else set()))
                aliases = self.short_aliases.get(token, set())
                if len(aliases) == 1:
                    alias = next(iter(aliases))
                    queue.extend((item, alias.rpartition("::")[0]) for item in identifiers(self.aliases[alias]))
        return result

    def _analyze(self):
        by_name = defaultdict(list)
        for function in self.functions:
            if function.owner not in self.classes:
                matches = self.types(function.owner, function.namespace, function.source)
                function.owner = next(iter(matches)) if len(matches) == 1 else ""
            if not function.owner:
                declaration = function_declarator(function.node.child_by_field_name("declarator"))
                signature = "::".join(part for part in (function.namespace, declarator_name(
                    declaration.child_by_field_name("declarator"))) if part)
                parameters = declaration.child_by_field_name("parameters")
                if parameters is not None:
                    signature += self.originals[function.source][parameters.start_byte:parameters.end_byte].decode("utf-8")
                self.files[function.source]["functions"].add(signature)
            function.result = self.types(text(function.node.child_by_field_name("type")),
                                         function.owner or function.namespace, function.source)
            by_name[function.name].append(function)
        for function in self.functions:
            variables = {name: self.types(value, function.owner or function.namespace, function.source)
                         for name, value in self.class_fields[function.owner].items()}
            variables["this"] = {function.owner} if function.owner else set()
            for node in walk(function.node):
                if node.type in ("type_identifier", "qualified_identifier", "base_class_clause"):
                    function.dependencies.update(self.types(text(node), function.owner or function.namespace, function.source))
                if node.type in ("parameter_declaration", "optional_parameter_declaration", "declaration"):
                    value = self.types(text(node.child_by_field_name("type")), function.owner or function.namespace, function.source)
                    for declaration in node.children_by_field_name("declarator"):
                        variables[declarator_name(declaration)] = value
            # Resolve auto variables initialized by constructors/factories using
            # declared return types. Iterate for chains of local initializers.
            for _ in range(len(variables) + 1):
                changed = False
                for node in walk(function.node):
                    if node.type != "init_declarator":
                        continue
                    name = declarator_name(node)
                    inferred = self.expression_types(node.child_by_field_name("value"), function, variables, by_name)
                    combined = variables.get(name, set()) | inferred
                    if combined != variables.get(name, set()):
                        variables[name] = combined
                        changed = True
                if not changed:
                    break
            for node in walk(function.node):
                if node.type != "call_expression":
                    continue
                expression = node.child_by_field_name("function")
                candidates = self.call_targets(expression, function, variables, by_name)
                function.dependencies.update(self.types(text(expression), function.namespace, function.source))
                for candidate in candidates:
                    function.calls.add(candidate.identity)
            function.dependencies.update(function.result)
            for values in variables.values():
                function.dependencies.update(values)
        self.by_identity = {function.identity: function for function in self.functions}

    def call_targets(self, expression, function, variables, by_name):
        if expression is None:
            return []
        name = re.sub(r"<.*>", "", text(expression))
        if expression.type == "field_expression":
            receiver = expression.child_by_field_name("argument")
            method = text(expression.child_by_field_name("field"))
            receiver_types = self.expression_types(receiver, function, variables, by_name)
            return [item for item in by_name[method] if item.owner in receiver_types]
        qualifier, _, simple = name.rpartition("::")
        candidates = by_name[simple or name]
        if qualifier:
            owners = self.types(qualifier, function.namespace, function.source)
            return [item for item in candidates if item.owner in owners or (
                not item.owner and item.namespace in (qualifier, function.namespace + "::" + qualifier))]
        candidates = [item for item in candidates if (
            item.owner and item.owner == function.owner) or (
            not item.owner and item.namespace == function.namespace)]
        local = [item for item in candidates if item.source == function.source]
        return local or candidates

    def expression_types(self, node, function, variables, by_name):
        if node is None:
            return set()
        if node.type == "call_expression":
            candidates = self.call_targets(node.child_by_field_name("function"), function, variables, by_name)
            result = self.types(text(node.child_by_field_name("function")), function.namespace, function.source)
            for candidate in candidates:
                result.update(candidate.result)
            return result
        if node.type == "field_expression":
            owners = self.expression_types(node.child_by_field_name("argument"), function, variables, by_name)
            name = text(node.child_by_field_name("field"))
            result = set()
            for owner in owners:
                result.update(self.types(self.class_fields[owner].get(name, ""), owner.rpartition("::")[0]))
            return result
        if node.type in ("parenthesized_expression", "pointer_expression"):
            return set().union(*(self.expression_types(child, function, variables, by_name)
                                 for child in node.named_children))
        return variables.get(text(node), set()) | self.types(text(node), function.namespace, function.source)

    def add_doxygen(self, xml_dir, source_name):
        """Augment syntax edges with Doxygen-resolved member/call references."""
        members, references = {}, []
        for path in sorted(xml_dir.glob("*.xml")):
            for compound in ET.parse(path).getroot().findall("compounddef"):
                owner_name = compound.findtext("compoundname", "")
                owner_matches = self.types(owner_name)
                owner = next(iter(owner_matches)) if compound.get("kind") in ("class", "struct") and len(owner_matches) == 1 else ""
                for member in compound.findall("./sectiondef/memberdef"):
                    location = member.find("location")
                    if location is None:
                        continue
                    source = source_name(location.get("bodyfile", location.get("file", "")))
                    if source not in self.files:
                        continue
                    line = int(location.get("bodystart", location.get("line", "0")))
                    name = member.findtext("name", "").rsplit("::", 1)[-1]
                    candidates = [item for item in self.functions if item.source == source and item.name == name
                                  and (not owner or item.owner == owner)]
                    exact = [item for item in candidates if item.node.start_point.row + 1 <= line <= item.node.end_point.row + 1]
                    selected = exact or candidates
                    targets = {item.identity for item in selected}
                    if owner:
                        targets.add("class:" + owner)
                    members[member.get("id")] = targets
                    for reference in member.findall("references"):
                        references.append((targets, reference.get("refid")))
        for origins, ref in references:
            for origin in origins:
                function = self.by_identity.get(origin)
                if function is None:
                    continue
                for target in members.get(ref, ()):
                    if target.startswith("class:"):
                        function.dependencies.add(target[6:])
                    else:
                        function.calls.add(target)

    def class_graph(self):
        # Least fixed point follows arbitrary global-function chains, including
        # recursive helpers, without introducing function vertices in the graph.
        closure = {item.identity: set(item.dependencies) for item in self.functions}
        changed = True
        while changed:
            changed = False
            for function in self.functions:
                combined = set(closure[function.identity])
                for called in function.calls:
                    callee = self.by_identity[called]
                    combined.update(closure[called])
                    if callee.owner:
                        combined.add(callee.owner)
                if combined != closure[function.identity]:
                    closure[function.identity] = combined
                    changed = True
        edges = set()
        for function in self.functions:
            if function.owner:
                edges.update((function.owner, dependency) for dependency in closure[function.identity]
                             if dependency != function.owner)
        for owner, fields in self.class_fields.items():
            for value in fields.values():
                edges.update((owner, dependency) for dependency in self.types(value, owner.rpartition("::")[0])
                             if dependency != owner)
        for owner, signatures in self.class_signatures.items():
            for signature in signatures:
                edges.update((owner, dependency) for dependency in self.types(signature, owner)
                             if dependency != owner)
        return set(self.classes), edges
