// See https://aka.ms/new-console-template for more information
using System.Reflection;
using System.Runtime.Loader;
using System.Text;

Console.OutputEncoding = new UTF8Encoding(false);

if (args.Length == 0)
{
    Console.WriteLine("Usage: AssemblyDump <path-to-assembly>");
    return;
}

var targetAssemblyPath = Path.GetFullPath(args[0]);
var requestedType = args.Length >= 2 ? args[1] : null;
var findSubstring = args.Length >= 3 && string.Equals(requestedType, "--find", StringComparison.OrdinalIgnoreCase)
    ? args[2]
    : null;
if (!File.Exists(targetAssemblyPath))
{
    Console.WriteLine($"Assembly not found: {targetAssemblyPath}");
    return;
}

var runtimeDir = System.Runtime.InteropServices.RuntimeEnvironment.GetRuntimeDirectory();
var runtimeAssemblies = Directory.GetFiles(runtimeDir, "*.dll")
    .Where(p => !string.Equals(Path.GetFileName(p), "mscorlib.dll", StringComparison.OrdinalIgnoreCase));

var targetDir = Path.GetDirectoryName(targetAssemblyPath)!;
var neighborAssemblies = Directory.GetFiles(targetDir, "*.dll");

var resolver = new PathAssemblyResolver(runtimeAssemblies
    .Concat(neighborAssemblies)
    .Append(targetAssemblyPath)
    .Distinct(StringComparer.OrdinalIgnoreCase));

using var mlc = new MetadataLoadContext(resolver, "System.Private.CoreLib");
var assembly = mlc.LoadFromAssemblyPath(targetAssemblyPath);

var types = assembly.GetTypes();
var interfaceCandidates = types
    .Where(t => t.IsInterface && (t.Name.Contains("Video", StringComparison.OrdinalIgnoreCase)
        || t.Name.Contains("Writer", StringComparison.OrdinalIgnoreCase)
        || t.Name.Contains("Plugin", StringComparison.OrdinalIgnoreCase)))
    .OrderBy(t => t.FullName, StringComparer.OrdinalIgnoreCase)
    .ToList();

if (!string.IsNullOrWhiteSpace(findSubstring))
{
    var matches = types
        .Where(t => t.FullName != null && t.FullName.Contains(findSubstring, StringComparison.OrdinalIgnoreCase))
        .OrderBy(t => t.FullName, StringComparer.OrdinalIgnoreCase)
        .ToList();
    foreach (var m in matches)
    {
        Console.WriteLine(m.FullName);
    }
    return;
}

if (!string.IsNullOrWhiteSpace(requestedType))
{
    var target = types.FirstOrDefault(t => t.FullName == requestedType);
    if (target is null)
    {
        Console.WriteLine($"Type not found: {requestedType}");
        return;
    }

    DumpAnyType(target);
    return;
}

Console.WriteLine("== Interface Candidates ==");
foreach (var t in interfaceCandidates)
{
    Console.WriteLine(t.FullName);
}

Console.WriteLine();
DumpIfExists(types, "YukkuriMovieMaker.Plugin.FileWriter.IVideoFileWriterPlugin");
DumpIfExists(types, "YukkuriMovieMaker.Plugin.FileWriter.IVideoFileWriter");
DumpIfExists(types, "YukkuriMovieMaker.Plugin.FileWriter.IVideoFileWriter2");

static void DumpType(Type type)
{
    Console.WriteLine(type.FullName);
    var baseTypes = type.GetInterfaces()
        .Select(i => i.FullName)
        .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
        .ToList();
    if (baseTypes.Count > 0)
    {
        Console.WriteLine("  Interfaces:");
        foreach (var bt in baseTypes)
        {
            Console.WriteLine($"    {bt}");
        }
    }

    Console.WriteLine("  Properties:");
    foreach (var p in type.GetProperties().OrderBy(p => p.Name, StringComparer.OrdinalIgnoreCase))
    {
        Console.WriteLine($"    {p.PropertyType.FullName} {p.Name} {{ get; {(p.SetMethod != null ? "set; " : string.Empty)}}}");
    }

    // Avoid method inspection for non-interface types due to MetadataLoadContext limitations.
}

static void DumpAnyType(Type type)
{
    Console.WriteLine(type.FullName);
    if (type.IsEnum)
    {
        Console.WriteLine("  Enum:");
        foreach (var field in type.GetFields().Where(f => f.IsLiteral))
        {
            var rawValue = field.GetRawConstantValue();
            Console.WriteLine($"    {field.Name} = {rawValue}");
        }
        return;
    }

    if (type.IsInterface)
    {
        DumpType(type);
        return;
    }

    Console.WriteLine("  Properties:");
    foreach (var p in type.GetProperties().OrderBy(p => p.Name, StringComparer.OrdinalIgnoreCase))
    {
        Console.WriteLine($"    {p.PropertyType.FullName} {p.Name} {{ get; {(p.SetMethod != null ? "set; " : string.Empty)}}}");
    }

    // Method inspection for non-interface types is intentionally skipped.
}

static void DumpIfExists(Type[] types, string fullName)
{
    var t = types.FirstOrDefault(x => x.IsInterface && x.FullName == fullName);
    if (t is null)
    {
        Console.WriteLine($"Target interface not found: {fullName}");
        Console.WriteLine();
        return;
    }

    Console.WriteLine($"== {t.Name} ==");
    DumpType(t);
    Console.WriteLine();
}
