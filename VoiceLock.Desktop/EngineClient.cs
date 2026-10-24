using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json.Nodes;

namespace VoiceLock.Desktop;
public sealed class EngineClient : IDisposable
{
    private Process? process;
    private NamedPipeClientStream? pipe;
    private StreamReader? reader;
    private StreamWriter? writer;
    private readonly SemaphoreSlim gate = new(1);
    private string lastError = "";
    public bool Busy => gate.CurrentCount == 0;
    public async Task Connect()
    {
        var name = "VoiceLock-" + Guid.NewGuid().ToString("N");
        var dir = AppContext.BaseDirectory;
        var exe = Path.Combine(dir, "worker", "VoiceLock.AudioEngine.exe");
        var start = new ProcessStartInfo { UseShellExecute = false, CreateNoWindow = true, RedirectStandardError = true };
        if (File.Exists(exe)) start.FileName = exe;
        else
        {
            var root = new DirectoryInfo(dir);
            while (root != null && !File.Exists(Path.Combine(root.FullName, "worker.py"))) root = root.Parent;
            if (root == null) throw new FileNotFoundException("Packaged worker is missing. Run scripts/build-native.ps1.");
            start.FileName = Path.Combine(root.FullName, "native", "build", "Release", "VoiceLock.AudioEngine.exe");
            if (!File.Exists(start.FileName))
                throw new FileNotFoundException("Build the C++ worker with scripts/build-native.ps1 first.");
            start.WorkingDirectory = root.FullName;
        }
        start.ArgumentList.Add("serve"); start.ArgumentList.Add("--pipe"); start.ArgumentList.Add(name);
        process = Process.Start(start) ?? throw new IOException("Unable to launch audio worker");
        process.ErrorDataReceived += (_, e) => { if (e.Data != null) lastError = e.Data.Length > 1200 ? e.Data[^1200..] : e.Data; };
        process.BeginErrorReadLine();
        pipe = new NamedPipeClientStream(".", name, PipeDirection.InOut, PipeOptions.Asynchronous);
        try { await pipe.ConnectAsync(30000); }
        catch (Exception e) { Dispose(); throw new IOException("Audio worker could not connect. " + lastError, e); }
        reader = new StreamReader(pipe, new UTF8Encoding(false), false, 8192, true);
        writer = new StreamWriter(pipe, new UTF8Encoding(false), 8192, true) { AutoFlush = true };
    }
    public async Task<JsonNode> Call(string op, JsonObject? payload = null)
    {
        await gate.WaitAsync();
        try
        {
            payload ??= new JsonObject(); payload["op"] = op;
            await writer!.WriteLineAsync(payload.ToJsonString());
            var line = await reader!.ReadLineAsync() ?? throw new IOException("Audio worker disconnected. Output is muted.");
            var response = JsonNode.Parse(line)!;
            if (!response["ok"]!.GetValue<bool>()) throw new InvalidOperationException(response["error"]!.GetValue<string>());
            return response["result"]!;
        }
        finally { gate.Release(); }
    }
    public void Dispose()
    {
        pipe?.Dispose();
        if (process is { HasExited: false })
        {
            if (!process.WaitForExit(1500)) process.Kill(true);
        }
        process?.Dispose();
    }
}
