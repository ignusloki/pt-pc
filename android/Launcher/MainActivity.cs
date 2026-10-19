using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Globalization;
using Android;
using Android.App;
using Android.Content;
using Android.Content.PM;
using Android.OS;
using Android.Views;
using Android.Widget;
using PT.PkgExtract;

namespace PT.AndroidLauncher;

[Activity(Name = "org.ignusloki.pt.prototype.LauncherActivity", MainLauncher = true, Exported = true,
    ConfigurationChanges = ConfigChanges.Orientation | ConfigChanges.ScreenSize)]
public sealed class MainActivity : Activity {
    const int PickPackage = 10, MicrophoneRequest = 11, SaveDiagnostics = 12;
    const long GiB = 1024L * 1024 * 1024;
    TextView status = null!;
    Button select = null!, play = null!, diagnostics = null!;
    CheckBox microphone = null!, touch = null!;
    Spinner subtitleSize = null!;
    static readonly string[] SubtitleScales = { "1", "1.25", "1.5", "1.75", "2" };
    ProgressBar progress = null!;
    bool busy;
    TaskCompletionSource<bool>? microphoneAnswer;
    string Root => FilesDir!.AbsolutePath;
    string Game => Path.Combine(Root, "game");
    string Settings => Path.Combine(Root, "profile", "pt.ini");

    protected override void OnCreate(Bundle? state) {
        base.OnCreate(state);
        var layout = new LinearLayout(this) { Orientation = Orientation.Vertical };
        layout.SetPadding(28, 64, 28, 32);
        layout.SetBackgroundColor(Android.Graphics.Color.Rgb(18, 22, 20));
        var heading = new TextView(this) { Text = ApplicationInfo!.LoadLabel(PackageManager!)?.ToString(), TextSize = 26 };
        heading.SetTextColor(Android.Graphics.Color.White);
        layout.AddView(heading);
        var info = new TextView(this) {
            Text = "Select your P.T. package to install the game files on this phone.\n\nConnect a controller, or enable the basic touch controls below. This experimental build starts with reduced graphics.",
            TextSize = 16
        };
        info.SetPadding(0, 24, 0, 24);
        layout.AddView(info);
        select = new Button(this) { Text = "Select P.T. .pkg" };
        select.Click += (_, _) => {
            var intent = new Intent(Intent.ActionOpenDocument);
            intent.AddCategory(Intent.CategoryOpenable);
            intent.SetType("*/*");
            intent.AddFlags(ActivityFlags.GrantReadUriPermission);
            StartActivityForResult(intent, PickPackage);
        };
        layout.AddView(select);
        microphone = new CheckBox(this) { Text = "Enable phone microphone", Checked = ReadSetting("voice", "microphone_enabled", "1") != "0" };
        microphone.CheckedChange += (_, e) => WriteSetting("voice", "microphone_enabled", e.IsChecked ? "1" : "0");
        layout.AddView(microphone);
        var preferences = GetPreferences(FileCreationMode.Private)!;
        touch = new CheckBox(this) { Text = "Basic touch controls", Checked = preferences.GetBoolean("touch_controls", true) };
        touch.CheckedChange += (_, e) => preferences.Edit()!.PutBoolean("touch_controls", e.IsChecked)!.Apply();
        layout.AddView(touch);
        layout.AddView(new TextView(this) { Text = "Subtitle size", TextSize = 16 });
        subtitleSize = new Spinner(this) {
            Adapter = new ArrayAdapter<string>(this, Android.Resource.Layout.SimpleSpinnerDropDownItem,
                new[] { "100% (original)", "125%", "150%", "175%", "200%" })
        };
        subtitleSize.SetSelection(ReadSubtitleSize());
        subtitleSize.ItemSelected += (_, e) => {
            if (e.Position != ReadSubtitleSize()) WriteSetting("display", "subtitle_scale", SubtitleScales[e.Position]);
        };
        layout.AddView(subtitleSize);
        play = new Button(this) { Text = "Play P.T." };
        play.Click += async (_, _) => await LaunchGame();
        layout.AddView(play);
        progress = new ProgressBar(this) { Visibility = ViewStates.Gone };
        layout.AddView(progress);
        status = new TextView(this) { TextSize = 15 };
        status.SetPadding(0, 20, 0, 0);
        layout.AddView(status);
        diagnostics = new Button(this) { Text = "Save diagnostic log" };
        diagnostics.Click += (_, _) => {
            var intent = new Intent(Intent.ActionCreateDocument);
            intent.AddCategory(Intent.CategoryOpenable);
            intent.SetType("text/plain");
            intent.PutExtra(Intent.ExtraTitle, "PT-Android-diagnostics.txt");
            StartActivityForResult(intent, SaveDiagnostics);
        };
        layout.AddView(diagnostics);
        var scroll = new ScrollView(this);
        scroll.SetBackgroundColor(Android.Graphics.Color.Rgb(18, 22, 20));
        scroll.SetOnApplyWindowInsetsListener(new LauncherInsets());
        scroll.AddView(layout);
        SetContentView(scroll);
        scroll.RequestApplyInsets();
        UpdateReady();
    }

    sealed class LauncherInsets : Java.Lang.Object, View.IOnApplyWindowInsetsListener {
        public WindowInsets? OnApplyWindowInsets(View? view, WindowInsets? insets) {
            if (view == null || insets == null) return insets;
            if (OperatingSystem.IsAndroidVersionAtLeast(30)) {
                var safe = insets.GetInsets(WindowInsets.Type.SystemBars() | WindowInsets.Type.DisplayCutout())!;
                view.SetPadding(safe.Left, safe.Top, safe.Right, safe.Bottom);
            } else {
                var cutout = insets.DisplayCutout;
                view.SetPadding(Math.Max(insets.SystemWindowInsetLeft, cutout?.SafeInsetLeft ?? 0),
                    Math.Max(insets.SystemWindowInsetTop, cutout?.SafeInsetTop ?? 0),
                    Math.Max(insets.SystemWindowInsetRight, cutout?.SafeInsetRight ?? 0),
                    Math.Max(insets.SystemWindowInsetBottom, cutout?.SafeInsetBottom ?? 0));
            }
            return insets;
        }
    }

    protected override void OnResume() {
        base.OnResume();
        if (status != null && !busy) {
            microphone.Checked = ReadSetting("voice", "microphone_enabled", "1") != "0";
            subtitleSize.SetSelection(ReadSubtitleSize());
            UpdateReady();
        }
    }

    void UpdateReady() {
        if (busy) Window!.AddFlags(WindowManagerFlags.KeepScreenOn);
        else Window!.ClearFlags(WindowManagerFlags.KeepScreenOn);
        play.Enabled = !busy && File.Exists(Path.Combine(Game, "chunk1.psarc")) && File.Exists(Path.Combine(Game, "texture.qar"));
        select.Enabled = !busy;
        diagnostics.Enabled = !busy;
        microphone.Enabled = touch.Enabled = !busy;
        subtitleSize.Enabled = !busy;
        progress.Visibility = busy ? ViewStates.Visible : ViewStates.Gone;
        if (!busy) status.Text = play.Enabled ? "Game files are installed. Ready to play." : "No game files installed yet.";
    }

    void Report(string message) {
        Android.Util.Log.Info("PTLauncher", message);
        File.AppendAllText(Path.Combine(Root, "launcher.log"), DateTime.UtcNow.ToString("O") + " " + message + "\n");
        RunOnUiThread(() => status.Text = message);
    }

    protected override async void OnActivityResult(int requestCode, Result resultCode, Intent? data) {
        base.OnActivityResult(requestCode, resultCode, data);
        if (requestCode == SaveDiagnostics && resultCode == Result.Ok && data?.Data != null) {
            try {
                using var output = ContentResolver!.OpenOutputStream(data.Data) ?? throw new IOException("Cannot write the diagnostic log.");
                using var writer = new StreamWriter(output, Encoding.UTF8);
                writer.Write(BuildDiagnostics());
                status.Text = "Diagnostic log saved. Review it before attaching it to a bug report.";
            } catch (Exception e) {
                status.Text = "Could not save diagnostic log: " + e.Message;
            }
            return;
        }
        if (requestCode != PickPackage || resultCode != Result.Ok || data?.Data == null || busy) return;
        busy = true;
        UpdateReady();
        string import = Path.Combine(Root, ".import-" + Guid.NewGuid().ToString("N"));
        try {
            await Task.Run(async () => {
                // The document can live in Downloads, an SD card, or a cloud provider. A seekable private copy is needed by PFS.
                using var input = ContentResolver!.OpenInputStream(data.Data) ?? throw new IOException("Cannot open the selected package.");
                if (new StatFs(Root).AvailableBytes < 4 * GiB) throw new IOException("At least 4 GB of free space is needed to import a package.");
                Directory.CreateDirectory(import);
                string package = Path.Combine(import, "source.pkg"), extracted = Path.Combine(import, "game");
                Report("Copying the selected package…");
                using (var output = new FileStream(package, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1024 * 1024, true)) {
                    byte[] buffer = new byte[1024 * 1024];
                    long copied = 0, nextReport = 64 * 1024 * 1024;
                    int count;
                    while ((count = await input.ReadAsync(buffer)) > 0) {
                        copied += count;
                        if (copied > 4 * GiB) throw new IOException("This Android build accepts packages up to 4 GB.");
                        await output.WriteAsync(buffer.AsMemory(0, count));
                        if (copied >= nextReport) {
                            Report($"Copying package: {copied / (1024 * 1024)} MB");
                            nextReport = copied + 64 * 1024 * 1024;
                        }
                    }
                }
                Extraction.Run(package, extracted, Report);
                Report("Verifying extracted game files…");
                foreach (string name in new[] { "chunk1.psarc", "texture.qar" }) {
                    using var file = File.OpenRead(Path.Combine(extracted, name));
                    Report($"SHA256 {name} {Convert.ToHexString(SHA256.HashData(file)).ToLowerInvariant()}");
                }
                // Commit only a complete import. Replacing assets never replaces profile/settings/saves.
                string previous = Path.Combine(import, "previous-game");
                bool replaced = Directory.Exists(Game);
                if (replaced) Directory.Move(Game, previous);
                try { Directory.Move(extracted, Game); }
                catch {
                    if (replaced) Directory.Move(previous, Game);
                    throw;
                }
                Report("P.T. package imported successfully.");
            });
        } catch (Exception e) {
            Report("Import failed: " + e.Message);
            new AlertDialog.Builder(this).SetTitle("Installation stopped").SetMessage(e.Message).SetPositiveButton("OK", (_, _) => { }).Show();
        } finally {
            if (Directory.Exists(import)) Directory.Delete(import, true);
            busy = false;
            UpdateReady();
        }
    }

    string BuildDiagnostics() {
        var text = new StringBuilder();
        var package = PackageManager!.GetPackageInfo(PackageName!, PackageInfoFlags.MetaData);
        text.AppendLine($"P.T. Android diagnostics — {DateTime.UtcNow:O}");
        text.AppendLine($"App: {PackageName} {package!.VersionName}");
        text.AppendLine($"Phone: {Build.Manufacturer} {Build.Model}; Android {Build.VERSION.Release}; API {(int)Build.VERSION.SdkInt}");
        text.AppendLine("ABIs: " + string.Join(", ", Build.SupportedAbis ?? Array.Empty<string>()));
        text.AppendLine("Touch controls: " + GetPreferences(FileCreationMode.Private)!.GetBoolean("touch_controls", true));
        text.AppendLine("Subtitle size: " + SubtitleScales[ReadSubtitleSize()]);
        foreach (var (relative, limit) in new[] { ("launcher.log", 16 * 1024), ("profile/pt.log", 64 * 1024),
                                                ("profile/vulkan-capabilities.txt", 128 * 1024) }) {
            text.AppendLine($"\n--- {relative} (last {limit / 1024} KiB) ---");
            string path = Path.Combine(Root, relative);
            if (!File.Exists(path)) { text.AppendLine("No log yet."); continue; }
            using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            file.Seek(Math.Max(0, file.Length - limit), SeekOrigin.Begin);
            using var reader = new StreamReader(file, Encoding.UTF8);
            text.AppendLine(reader.ReadToEnd().Replace(Root, "<app data>", StringComparison.Ordinal));
        }
        return text.ToString();
    }

    async Task StageRuntime() {
        using var stream = Assets!.Open("runtime-manifest.json");
        using var reader = new StreamReader(stream);
        string manifest = await reader.ReadToEndAsync();
        string stamp = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(manifest)));
        string runtime = Path.Combine(Root, "runtime"), stampFile = Path.Combine(runtime, "installed-manifest.txt");
        if (File.Exists(stampFile) && File.ReadAllText(stampFile) == stamp) return;
        await Task.Run(() => {
            Report("Preparing game fonts, shaders and speech models…");
            string pending = Path.Combine(Root, ".runtime-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(pending);
            try {
                var files = JsonSerializer.Deserialize<Dictionary<string, string>>(manifest)!;
                foreach (var (name, expected) in files) {
                    if (!name.StartsWith("runtime/", StringComparison.Ordinal)) continue;
                    string relative = name[8..];
                    string destination = Path.GetFullPath(Path.Combine(pending, relative));
                    if (!destination.StartsWith(pending + Path.DirectorySeparatorChar, StringComparison.Ordinal)) throw new IOException("Invalid runtime asset path.");
                    Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                    using (var input = Assets.Open(name))
                    using (var output = File.Create(destination)) input.CopyTo(output, 1024 * 1024);
                    using var verified = File.OpenRead(destination);
                    if (!Convert.ToHexString(SHA256.HashData(verified)).Equals(expected, StringComparison.OrdinalIgnoreCase)) throw new IOException("Runtime asset verification failed: " + relative);
                }
                File.WriteAllText(Path.Combine(pending, "installed-manifest.txt"), stamp);
                if (Directory.Exists(runtime)) Directory.Delete(runtime, true);
                Directory.Move(pending, runtime);
            } finally {
                if (Directory.Exists(pending)) Directory.Delete(pending, true);
            }
        });
    }

    async Task LaunchGame() {
        if (busy) return;
        busy = true;
        UpdateReady();
        try {
            await StageRuntime();
            WriteSetting("voice", "microphone_enabled", microphone.Checked ? "1" : "0");
            WriteSetting("display", "subtitle_scale", SubtitleScales[subtitleSize.SelectedItemPosition]);
            if (microphone.Checked && CheckSelfPermission(Manifest.Permission.RecordAudio) != Permission.Granted) {
                Report("Requesting microphone access before starting the game…");
                microphoneAnswer = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
                RequestPermissions(new[] { Manifest.Permission.RecordAudio }, MicrophoneRequest);
                bool granted = await microphoneAnswer.Task;
                Report(granted ? "Microphone permission granted." : "Microphone permission denied; the game will run without microphone input.");
            }
            var game = new Intent();
            game.SetClassName(PackageName!, "org.ignusloki.pt.prototype.GameActivity");
            game.PutExtra("touch_controls", touch.Checked);
            StartActivity(game);
        } catch (Exception e) {
            Report("Could not start: " + e.Message);
            new AlertDialog.Builder(this).SetTitle("Could not start P.T.").SetMessage(e.Message).SetPositiveButton("OK", (_, _) => { }).Show();
        } finally {
            busy = false;
            UpdateReady();
        }
    }

    public override void OnRequestPermissionsResult(int requestCode, string[] permissions, Permission[] results) {
        base.OnRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == MicrophoneRequest) microphoneAnswer?.TrySetResult(results.Length > 0 && results[0] == Permission.Granted);
    }

    int ReadSubtitleSize() {
        if (!float.TryParse(ReadSetting("display", "subtitle_scale", "1.5"), NumberStyles.Float,
                CultureInfo.InvariantCulture, out float scale) || !float.IsFinite(scale)) scale = 1.5f;
        return (int)Math.Round((Math.Clamp(scale, 1.0f, 2.0f) - 1.0f) * 4.0f);
    }

    string ReadSetting(string section, string key, string fallback) {
        if (!File.Exists(Settings)) return fallback;
        bool active = false;
        foreach (string line in File.ReadLines(Settings)) {
            string text = line.Trim();
            if (text.StartsWith('[')) active = text.Equals("[" + section + "]", StringComparison.OrdinalIgnoreCase);
            else if (active && text.Contains('=')) {
                var pair = text.Split('=', 2);
                if (pair[0].Trim().Equals(key, StringComparison.OrdinalIgnoreCase)) return pair[1].Trim();
            }
        }
        return fallback;
    }

    void WriteSetting(string section, string key, string value) {
        Directory.CreateDirectory(Path.GetDirectoryName(Settings)!);
        var lines = File.Exists(Settings) ? File.ReadAllLines(Settings).ToList() : new List<string>();
        int start = lines.FindIndex(line => line.Trim().Equals("[" + section + "]", StringComparison.OrdinalIgnoreCase));
        if (start < 0) { lines.Add("[" + section + "]"); start = lines.Count - 1; }
        int end = start + 1;
        while (end < lines.Count && !lines[end].Trim().StartsWith('[')) ++end;
        int existing = lines.FindIndex(start + 1, end - start - 1, line => line.Split('=', 2)[0].Trim().Equals(key, StringComparison.OrdinalIgnoreCase));
        if (existing >= 0) lines[existing] = key + "=" + value;
        else lines.Insert(end, key + "=" + value);
        File.WriteAllLines(Settings + ".new", lines);
        File.Move(Settings + ".new", Settings, true);
    }
}
