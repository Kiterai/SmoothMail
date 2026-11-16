# Offline probe: no certificate-store, account or security-policy changes.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SchannelProbe {
    [StructLayout(LayoutKind.Sequential)] struct Handle { public IntPtr a, b; }
    [StructLayout(LayoutKind.Sequential)] struct Cred {
        public uint version, count;
        public IntPtr certs, store;
        public uint mappers;
        public IntPtr mapper;
        public uint supportedAlgorithms;
        public IntPtr algorithms;
        public uint protocols, minStrength, maxStrength, lifespan, flags, format;
    }
    [DllImport("secur32.dll", CharSet=CharSet.Unicode)]
    static extern int AcquireCredentialsHandleW(string principal, string package,
        uint use, IntPtr logon, ref Cred cred, IntPtr getKey, IntPtr arg,
        out Handle handle, out long expiry);
    [DllImport("secur32.dll")] static extern int FreeCredentialsHandle(ref Handle h);
    [DllImport("ncrypt.dll", CharSet=CharSet.Unicode)]
    static extern int NCryptOpenStorageProvider(out IntPtr h, string name, uint flags);
    [DllImport("ncrypt.dll")] static extern int NCryptFreeObject(IntPtr h);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("advapi32.dll", SetLastError=true)]
    static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll")] static extern bool IsTokenRestricted(IntPtr token);
    public static void Run() {
        IntPtr token;
        if (!OpenProcessToken(GetCurrentProcess(), 8, out token))
            throw new System.ComponentModel.Win32Exception();
        Console.WriteLine("IsTokenRestricted={0}", IsTokenRestricted(token));
        CloseHandle(token);
        foreach (uint flags in new uint[] { 0, 0x10, 0x18, 0x400010 }) {
            Cred c = new Cred(); c.version = 4; c.flags = flags;
            Handle h; long expiry;
            int s = AcquireCredentialsHandleW(null, "Microsoft Unified Security Protocol Provider",
                2, IntPtr.Zero, ref c, IntPtr.Zero, IntPtr.Zero, out h, out expiry);
            Console.WriteLine("AcquireCredentials flags=0x{0:X8} status=0x{1:X8}", flags, s);
            if (s == 0) FreeCredentialsHandle(ref h);
        }
        IntPtr provider;
        int status = NCryptOpenStorageProvider(out provider, "Microsoft Software Key Storage Provider", 0);
        Console.WriteLine("NCryptOpenStorageProvider status=0x{0:X8}", status);
        if (status == 0) NCryptFreeObject(provider);
    }
}
'@
[SchannelProbe]::Run()
