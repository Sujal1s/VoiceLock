using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Security.Cryptography;

namespace VoiceLock.Desktop;

internal static class CableInstaller
{
    internal const string DownloadUrl = "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip";
    internal const string PackageHash = "B950E39F01AF1D04EA623C8F6D8EB9B6EA5C477C637295FABF20631C85116BFB";

    internal static async Task<string> PrepareAsync(string directory)
    {
        using var client = new HttpClient { Timeout = TimeSpan.FromMinutes(2) };
        var bytes = await client.GetByteArrayAsync(DownloadUrl);
        return await ExtractVerifiedAsync(bytes, directory);
    }

    internal static async Task<string> ExtractVerifiedAsync(byte[] bytes, string directory)
    {
        if (!Convert.ToHexString(SHA256.HashData(bytes)).Equals(PackageHash, StringComparison.Ordinal))
            throw new InvalidDataException("VB-CABLE download verification failed. No installer was run. Use the official website or update VoiceLock.");
        Directory.CreateDirectory(directory);
        var archive = Path.Combine(directory, "VBCABLE_Driver_Pack45.zip");
        await File.WriteAllBytesAsync(archive, bytes);
        var extracted = Path.Combine(directory, "package");
        ZipFile.ExtractToDirectory(archive, extracted);
        var installer = Path.Combine(extracted, "VBCABLE_Setup_x64.exe");
        if (!File.Exists(installer)) throw new FileNotFoundException("The verified package is missing its Windows x64 installer.");
        return installer;
    }
}
