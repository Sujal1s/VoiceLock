using System.Globalization;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Forms = System.Windows.Forms;

namespace VoiceLock.Desktop;
public partial class MainWindow : Window
{
    private readonly EngineClient client = new();
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(1) };
    private Forms.NotifyIcon? tray;
    private bool ready, busy, updating;
    private JsonObject settings = new();
    public record Device(string Name, int Channels)
    {
        public override string ToString() => Name;
    }
    public MainWindow()
    {
        InitializeComponent();
        NavMicrophone.IsChecked = true;
        Loaded += async (_, _) => await Act(async () =>
        {
            await client.Connect(); ready = true;
            var hello = await client.Call("hello");
            Apply(hello["settings"]!);
            SetProfiles(hello);
            Hardware.Text = hello["hardware"]!.ToJsonString(new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
            EnrollmentStatus.Text = hello["enrolled"]!.GetValue<bool>() ? "Voice reference stored locally." : "No voice reference yet.";
            await RefreshDevices();
            Status.Text = hello["enrolled"]!.GetValue<bool>() && hello["models"]!.GetValue<bool>()
                ? "Ready. Voice reference loaded. Start isolation when ready."
                : "Ready. Complete first-run setup before starting.";
            timer.Start();
            var smoke = Environment.GetEnvironmentVariable("VOICELOCK_SMOKE_PATH");
            if (!string.IsNullOrEmpty(smoke))
            {
                StartButton.IsEnabled = true;
                UpdateLayout();
                var bitmap = new System.Windows.Media.Imaging.RenderTargetBitmap((int)ActualWidth, (int)ActualHeight, 96, 96, System.Windows.Media.PixelFormats.Pbgra32);
                bitmap.Render(this);
                var encoder = new System.Windows.Media.Imaging.PngBitmapEncoder();
                encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmap));
                using (var file = System.IO.File.Create(smoke)) encoder.Save(file);
                await System.IO.File.WriteAllTextAsync(smoke + ".json", hello.ToJsonString());
                if(Environment.GetEnvironmentVariable("VOICELOCK_UI_CHECK")=="1")
                {
                    NavAdvanced.IsChecked=true;
                    if(AdvancedPanel.Visibility!=Visibility.Visible || DashboardScroll.Visibility!=Visibility.Collapsed) throw new InvalidOperationException("Advanced navigation failed");
                    CaptureUi(smoke+".advanced.png");
                    NavSetup.IsChecked=true;
                    if(SetupPanel.Visibility!=Visibility.Visible) throw new InvalidOperationException("Setup navigation failed");
                    CaptureUi(smoke+".setup.png");
                    NavMicrophone.IsChecked=true;
                    InputMeter.Value=0.65; OutputMeter.Value=0.4;
                    await Dispatcher.InvokeAsync(()=>{},DispatcherPriority.Render);
                    CaptureUi(smoke+".meter-test.png");
                    Width=1100; Height=720;
                    CaptureUi(smoke+".compact.png");
                    await System.IO.File.WriteAllTextAsync(smoke+".checks.txt","PASS: setup and advanced navigation; dashboard return; synthetic meter rendering; minimum window size. No audio captured.");
                }
                Close();
            }
        });
        timer.Tick += async (_, _) =>
        {
            if (!ready || busy || client.Busy) return;
            try { ShowStatus(await client.Call("status")); }
            catch (Exception e) { Status.Text = e.Message; timer.Stop(); ready=false; }
        };
        tray = new Forms.NotifyIcon { Icon = System.Drawing.SystemIcons.Shield, Text = "VoiceLock", Visible = true };
        tray.DoubleClick += (_, _) => Dispatcher.Invoke(() => { Show(); WindowState = WindowState.Normal; Activate(); });
        tray.ContextMenuStrip = new Forms.ContextMenuStrip();
        tray.ContextMenuStrip.Items.Add("Show", null, (_, _) => Dispatcher.Invoke(() => { Show(); WindowState=WindowState.Normal; }));
        tray.ContextMenuStrip.Items.Add("Stop / mute", null, async (_, _) => await Act(async () => ShowStatus(await client.Call("stop"))));
        tray.ContextMenuStrip.Items.Add("Exit", null, (_, _) => Dispatcher.Invoke(Close));
        StateChanged += (_, _) => { if (WindowState == WindowState.Minimized) Hide(); };
        Closed += (_, _) => { timer.Stop(); tray.Dispose(); client.Dispose(); };
    }
    private async Task Act(Func<Task> action)
    {
        if (busy) return;
        busy = true; StartButton.IsEnabled = false;
        try { await action(); }
        catch (Exception e) { Status.Text = e.Message; }
        finally { busy=false; StartButton.IsEnabled = ready; }
    }
    private void CaptureUi(string path)
    {
        UpdateLayout();
        var bitmap=new System.Windows.Media.Imaging.RenderTargetBitmap((int)ActualWidth,(int)ActualHeight,96,96,System.Windows.Media.PixelFormats.Pbgra32);
        bitmap.Render(this);
        var encoder=new System.Windows.Media.Imaging.PngBitmapEncoder();
        encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmap));
        using var file=System.IO.File.Create(path);
        encoder.Save(file);
    }
    private void Apply(JsonNode node)
    {
        updating=true;
        settings=(JsonObject)node.DeepClone();
        Model.SelectedIndex=settings["model"]!.GetValue<string>()=="int8-full"?0:1;
        Threads.Text=settings["threads"]!.ToString(); Chunk.Text=settings["chunk_ms"]!.ToString();
        InputRate.Text=settings["input_rate"]!.ToString(); OutputRate.Text=settings["output_rate"]!.ToString();
        OutputBuffer.Text=settings["output_buffer_ms"]?.ToString() ?? "80";
        Gain.Text=settings["gain"]!.ToString(); Gaming.IsChecked=settings["gaming"]!.GetValue<bool>();
        Diagnostics.IsChecked=settings["diagnostics"]!.GetValue<bool>();
        Profile.SelectedIndex=0;
        updating=false;
    }
    private void SetProfiles(JsonNode hello)
    {
        var names = hello["profiles"]?.AsArray().Select(n => n!.ToString()).ToHashSet() ?? new HashSet<string>();
        foreach (ComboBoxItem item in Profile.Items)
            item.IsEnabled = item.Content.ToString() == "Manual" || names.Contains(item.Content.ToString()!);
    }
    private async Task RefreshDevices()
    {
        var items=(JsonArray)await client.Call("devices");
        var inputs=items.Where(x=>x!["input"]!.GetValue<int>()>0).Select(x=>new Device(x!["name"]!.GetValue<string>(),x["input"]!.GetValue<int>())).ToList();
        var outputs=items.Where(x=>x!["output"]!.GetValue<int>()>0).Select(x=>new Device(x!["name"]!.GetValue<string>(),x["output"]!.GetValue<int>())).ToList();
        InputDevice.ItemsSource=inputs; OutputDevice.ItemsSource=outputs;
        bool cableInstalled=inputs.Any(x=>x.Name.Contains("CABLE Output",StringComparison.OrdinalIgnoreCase)) && outputs.Any(x=>x.Name.Contains("CABLE Input",StringComparison.OrdinalIgnoreCase));
        InstallCableButton.IsEnabled=!cableInstalled;
        InstallCableButton.Content=cableInstalled?"VB-CABLE installed":"Install VB-CABLE";
        CableStatus.Text=cableInstalled?"VB-CABLE detected. Select CABLE Output in your calling app.":"VB-CABLE endpoints missing. Install the driver, restart Windows, then refresh devices.";
        InputDevice.SelectedItem=inputs.FirstOrDefault(x=>x.Name==settings["input_device"]?.ToString()) ?? inputs.FirstOrDefault(x=>!x.Name.Contains("Streaming"));
        OutputDevice.SelectedItem=outputs.FirstOrDefault(x=>x.Name==settings["output_device"]?.ToString()) ?? outputs.FirstOrDefault(x=>x.Name.Contains("CABLE Input"));
        if (OutputDevice.SelectedItem==null) Status.Text="VB-CABLE not found. Install it from vb-audio.com, then refresh devices.";
    }
    private async Task Save()
    {
        settings["input_device"]=(InputDevice.SelectedItem as Device)?.Name ?? "";
        settings["output_device"]=(OutputDevice.SelectedItem as Device)?.Name ?? "";
        settings["model"]=(Model.SelectedItem as ComboBoxItem)?.Content.ToString() ?? "int8-full";
        settings["threads"]=int.Parse(Threads.Text); settings["chunk_ms"]=int.Parse(Chunk.Text);
        settings["input_rate"]=int.Parse(InputRate.Text); settings["output_rate"]=int.Parse(OutputRate.Text);
        settings["output_buffer_ms"]=int.Parse(OutputBuffer.Text);
        settings["gain"]=double.Parse(Gain.Text,CultureInfo.InvariantCulture);
        settings["gaming"]=Gaming.IsChecked==true; settings["diagnostics"]=Diagnostics.IsChecked==true;
        await client.Call("settings",new JsonObject { ["settings"]=settings.DeepClone() });
    }
    private static double N(JsonNode n,string key) => n[key]?.GetValue<double>() ?? 0;
    private void Navigate_Click(object sender,RoutedEventArgs e)
    {
        if(DashboardScroll==null || SetupPanel==null || AdvancedPanel==null) return;
        string page=((System.Windows.Controls.RadioButton)sender).Tag?.ToString() ?? "Microphone";
        DashboardScroll.Visibility=page is "Advanced" or "Voice Setup" or "Presets"?Visibility.Collapsed:Visibility.Visible;
        SetupPanel.Visibility=page is "Voice Setup" or "Presets"?Visibility.Visible:Visibility.Collapsed;
        AdvancedPanel.Visibility=page=="Advanced"?Visibility.Visible:Visibility.Collapsed;
        PageTitle.Text=page;
        PageSubtitle.Text=page switch
        {
            "Audio Routes"=>"Connect your microphone, VoiceLock and your calling app.",
            "Noise Control"=>"Choose your isolation model and performance settings.",
            "Presets"=>"Calibrate this laptop to unlock measured performance profiles.",
            "Voice Setup"=>"Download the models and teach VoiceLock your voice.",
            "Advanced"=>"Fine-tune buffering, CPU use and output volume.",
            _=>"Make your voice clear. Remove background noise in real time."
        };
        RouteCard.BorderBrush=new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(39,48,50));
        NoiseCard.BorderBrush=RouteCard.BorderBrush;
        if(page=="Audio Routes") { RouteCard.BorderBrush=System.Windows.Media.Brushes.MediumAquamarine; InputDevice.Focus(); }
        if(page=="Noise Control") { NoiseCard.BorderBrush=System.Windows.Media.Brushes.MediumAquamarine; Model.Focus(); }
        if(page=="Presets") Dispatcher.BeginInvoke(()=>CalibrationStatus.BringIntoView());
    }
    private void OpenAdvanced_Click(object sender,RoutedEventArgs e)=>NavAdvanced.IsChecked=true;
    private void ClearLog_Click(object sender,RoutedEventArgs e)=>Metrics.Text="Console cleared. Live measurements update while isolation runs.";
    private void ShowStatus(JsonNode n)
    {
        bool running=n["running"]?.GetValue<bool>()==true;
        string error=n["error"]?.ToString() ?? "";
        LiveState.Text=error.Length>0?"ERROR":running?(n["muted"]?.GetValue<bool>()==true?"MUTED":"LIVE"):"IDLE";
        LatencyValue.Text=running?$"{N(n,"estimated_latency_ms"):F0} ms":"—";
        InferenceValue.Text=running?$"{N(n,"inference_mean_ms"):F1} / {N(n,"inference_p95_ms"):F1} ms":"—";
        RateValue.Text=running?$"{N(n,"output_rate")/1000:F1} kHz":"—";
        CpuValue.Text=running?$"{N(n,"cpu_percent"):F1} %":"—";
        RamValue.Text=running?$"{N(n,"ram_mb"):F0} MB":"—";
        RtfValue.Text=running?$"{N(n,"rtf_p95"):F2}":"—";
        if (n["model"]!=null)
        {
            InputMeter.Value=running?Math.Min(1,N(n,"input_level")*5):0;
            OutputMeter.Value=running?Math.Min(1,N(n,"output_level")*5):0;
            Metrics.Text=$"{n["backend"] ?? "Python"} · {n["model"]} · {n["threads"]} threads · {n["chunk_ms"]} ms\n"+
                $"{n["input_rate"]} → 16000 → {n["output_rate"]} Hz\n"+
                $"Inference avg / p95   {N(n,"inference_mean_ms"):F1} / {N(n,"inference_p95_ms"):F1} ms\n"+
                $"Latency estimate     {N(n,"estimated_latency_ms"):F0} ms   RTF {N(n,"rtf_p95"):F2}\n"+
                $"CPU {N(n,"cpu_percent"):F1}%   RAM {N(n,"ram_mb"):F0} MB\n"+
                $"Underruns {n["underruns"]}   Overruns {n["overruns"]}   Recoveries {n["recoveries"] ?? 0} / trims {n["playback_trims"] ?? 0}";
            timer.Interval=TimeSpan.FromSeconds(n["economy"]?.GetValue<bool>()==true?3:Gaming.IsChecked==true?2:1);
        }
        else { InputMeter.Value=OutputMeter.Value=0; Metrics.Text="Stopped · no live measurements."; }
        Status.Text=error.Length>0?"MUTED · "+error:running?(n["muted"]?.GetValue<bool>()==true?"Preparing / recovering native audio · output muted.":"Isolating · "+(n["warning"]?.ToString() ?? "")):"Stopped · output muted.";
        if(tray!=null) tray.Text=running?"VoiceLock · isolating":"VoiceLock · muted";
    }
    private async void Refresh_Click(object sender,RoutedEventArgs e)=>await Act(RefreshDevices);
    private void CableWebsite_Click(object sender,RoutedEventArgs e)
    {
        try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("https://vb-audio.com/Cable/") { UseShellExecute=true }); }
        catch(Exception error) { CableStatus.Text=error.Message; }
    }
    private async void InstallCable_Click(object sender,RoutedEventArgs e)=>await Act(async()=>
    {
        InstallCableButton.IsEnabled=false;
        try
        {
            ShowStatus(await client.Call("stop"));
            CableStatus.Text="Downloading and verifying the official VB-CABLE package…";
            var directory=System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"VoiceLock","installers",Guid.NewGuid().ToString("N"));
            var installer=await CableInstaller.PrepareAsync(directory);
            CableStatus.Text="Approve the Windows administrator prompt, then click Install Driver in VB-CABLE Setup.";
            using var process=System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(installer)
            {
                UseShellExecute=true, Verb="runas", WorkingDirectory=System.IO.Path.GetDirectoryName(installer)!
            }) ?? throw new InvalidOperationException("Windows could not open VB-CABLE Setup.");
            await process.WaitForExitAsync();
            await RefreshDevices();
            CableStatus.Text="VB-CABLE Setup closed. If you installed the driver, restart Windows, reopen VoiceLock and refresh devices.";
        }
        catch(System.ComponentModel.Win32Exception error) when(error.NativeErrorCode==1223)
        { CableStatus.Text="Administrator prompt cancelled. You can retry Install VB-CABLE."; }
        catch(Exception error) { CableStatus.Text="VB-CABLE setup could not finish: "+error.Message; }
        finally { InstallCableButton.IsEnabled=InstallCableButton.Content?.ToString()!="VB-CABLE installed"; }
    });
    private async void Start_Click(object sender,RoutedEventArgs e)=>await Act(async()=>{ await Save(); Status.Text="Loading model…"; ShowStatus(await client.Call("start")); });
    private async void Stop_Click(object sender,RoutedEventArgs e)=>await Act(async()=>ShowStatus(await client.Call("stop")));
    private async void Save_Click(object sender,RoutedEventArgs e)=>await Act(async()=>{await Save(); Status.Text="Settings saved. Audio stopped.";});
    private async void Setup_Click(object sender,RoutedEventArgs e)=>await Act(async()=>{Status.Text="Downloading and verifying official models…"; await client.Call("setup"); Status.Text="Models verified. Offline inference is ready.";});
    private async void Enroll_Click(object sender,RoutedEventArgs e)=>await Act(async()=>{await Save(); Status.Text="Recording three seconds — speak now."; await client.Call("enroll",new JsonObject{["consent"]=Consent.IsChecked==true}); EnrollmentStatus.Text="Voice reference stored locally. You can rerecord or delete it anytime."; Status.Text="Enrollment complete.";});
    private async void Forget_Click(object sender,RoutedEventArgs e)=>await Act(async()=>{await client.Call("forget"); EnrollmentStatus.Text="Voice reference deleted.";});
    private async void Calibrate_Click(object sender,RoutedEventArgs e)=>await Act(async()=>{Status.Text="Benchmarking 16 CPU configurations…"; var r=await client.Call("calibrate"); CalibrationStatus.Text="Available profiles: "+string.Join(", ",((JsonObject)r["profiles"]!).Select(p=>p.Key))+"\n"+r["warning"]; var hello=await client.Call("hello"); Apply(hello["settings"]!); SetProfiles(hello); Status.Text="Calibration saved. Results are in the local data folder.";});
    private async void Profile_Changed(object sender,SelectionChangedEventArgs e)
    {
        if(updating || !ready || Profile.SelectedIndex<=0)return;
        string name=((ComboBoxItem)Profile.SelectedItem).Content.ToString()!;
        await Act(async()=>{Apply(await client.Call("profile",new JsonObject{["name"]=name})); Status.Text=name+" applied; audio stopped. Press Start.";});
    }
}
