using VoiceLock.Desktop;

var bytes=await File.ReadAllBytesAsync(args[0]);
var destination=Path.Combine(Path.GetFullPath(args[1]),Guid.NewGuid().ToString("N"));
var installer=await CableInstaller.ExtractVerifiedAsync(bytes, destination);
if(!File.Exists(installer) || Directory.GetFiles(Path.GetDirectoryName(installer)!,"*.inf").Length==0)
    throw new Exception("Missing installer or driver companions");
bytes[0]^=1;
var rejected=destination+"-rejected";
try
{
    await CableInstaller.ExtractVerifiedAsync(bytes,rejected);
    throw new Exception("Tampered package was accepted");
}
catch(InvalidDataException)
{
    if(Directory.Exists(rejected)) throw new Exception("Rejected download wrote files");
}
Console.WriteLine("PASS: official package extracts with driver files; tampered package rejected before writing. No installer launched.");
