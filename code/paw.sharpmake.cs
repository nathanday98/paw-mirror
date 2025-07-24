using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Security.Cryptography.X509Certificates;
using Sharpmake;
using Sharpmake.Generators.JsonCompilationDatabase;
using System.Text.Json;


public static class Globals
{
	public static string RootDir;
	public static string CodeRootDir;
	public static string CodeSrcDir;
	public static string ThirdPartyDir;
	public static string BuildDir;
	public static string ToolsBuildDir;
	public static string TempDir;
	public static string ProjectFilesDir;

	public static Platform SupportedPlatforms = Platform.win64;

	public static void InitPaths()
	{
		FileInfo file_info = Util.GetCurrentSharpmakeFileInfo();
		Globals.CodeRootDir = Util.SimplifyPath(Path.Combine(file_info.DirectoryName));
		Globals.CodeSrcDir = Util.SimplifyPath(Path.Combine(file_info.DirectoryName, "src"));
		Globals.RootDir = Util.SimplifyPath(Path.Combine(file_info.DirectoryName, ".."));
		Globals.TempDir = Path.Combine(Globals.RootDir, "temp");
		Globals.BuildDir = Path.Combine(Globals.RootDir, "build");
		Globals.ProjectFilesDir = Path.Combine(Globals.CodeRootDir, "project_files");
		Globals.ThirdPartyDir = Path.Combine(Globals.CodeRootDir, "third-party");
		Globals.ToolsBuildDir = Path.Combine(Globals.RootDir, "tools_build");
	}

	public static ITarget[] GetTargets()
	{

		return new[]
		{
			new CustomTarget{
				DevEnv = DevEnv.vs2022,
				Platform = SupportedPlatforms,
				Optimization = Optimization.Debug | Optimization.Release | Optimization.Retail,
				BuildSystem = BuildSystem.FastBuild,
				Framework = DotNetFramework.net6_0,
			}
		};
	}

	public static void ApplyLanguageSettings(Project.Configuration conf, CustomTarget target)
	{
		conf.Options.Add(Options.Vc.General.WarningLevel.Level4);
		conf.Options.Add(Options.Vc.General.TreatWarningsAsErrors.Enable);
		conf.Options.Add(new Options.Vc.Compiler.DisableSpecificWarnings(
			"4514", // unreferenced inline function has been removed
			"4710", // function not inlined
			"4820", // padding added to struct
			"5045", // Compiler will insert Spectre mitigation for memory load if / Qspectre switch specified 
			"4711", // function selected for automatic inline expansion
			"4206", // translation unit is empty,
			"4577", // 'noexcept' used with no exception handling mode specified; termination on exception is not guaranteed
			"4530", // C++ exception handler used, but unwind semantics are not enabled.Specify /EHsc
			"4505", // unreferenced function with internal linkage has been removed
			"4201", // nonstandard extension used: nameless struct/union
			"4127" // conditional expression is constant
		));
		conf.Options.Add(Options.Vc.Compiler.Exceptions.Disable);
		conf.Options.Add(Options.Vc.Compiler.CppLanguageStandard.CPP20);
		conf.Options.Add(Options.Vc.Compiler.RTTI.Enable);
		conf.Options.Add(Options.Vc.Compiler.ConformanceMode.Enable);
		conf.Options.Add(Options.Vc.Compiler.SupportJustMyCode.No);
		// conf.Options.Add(Options.Vc.Compiler.EnableAsan.Enable);

		if (target.Optimization == Optimization.Debug)
		{
			conf.Options.Add(Options.Vc.General.DebugInformation.ProgramDatabase);
			conf.Options.Add(Options.Vc.Compiler.FunctionLevelLinking.Enable);
			conf.Options.Add(Options.Vc.Linker.Incremental.Enable);
			//conf.Options.Add(Options.Vc.Linker.GenerateFullProgramDatabaseFile.Enable);
			conf.Options.Add(Options.Vc.Compiler.Inline.OnlyInline);
		}


		if (target.Optimization == Optimization.Debug)
		{
			conf.Options.Add(Options.Vc.Compiler.RuntimeLibrary.MultiThreadedDebugDLL);
		}
		else
		{
			conf.Options.Add(Options.Vc.Compiler.RuntimeLibrary.MultiThreadedDLL);
		}

		conf.Options.Add(Options.Vc.General.PlatformToolset.ClangCL);

		//conf.AdditionalCompilerOptions.Add("-fsanitize=undefined");
		//conf.AdditionalCompilerOptions.Add("-fsanitize=unsigned-integer-overflow");

		//conf.LibraryFiles.Add("clang_rt.ubsan_standalone-x86_64.lib");
		//conf.LibraryFiles.Add("clang_rt.ubsan_standalone_cxunux-x86_64.lib");

		conf.AdditionalCompilerOptions.Add("-Wno-pragma-once-outside-header");
		conf.AdditionalCompilerOptions.Add("-Wno-missing-field-initializers");
		conf.AdditionalCompilerOptions.Add("-Wno-unused-function");
		conf.AdditionalCompilerOptions.Add("-Wno-unused-const-variable");
		conf.AdditionalCompilerOptions.Add("-Wno-unused-private-field");

		conf.Defines.Add("_CRT_SECURE_NO_WARNINGS");
		conf.IsFastBuild = true;
		conf.AdditionalCompilerOptions.Add("/FS"); // Forces writes to the program database (PDB) file�created by /Zi or /ZI�to be serialized through MSPDBSRV.EXE. - This was in the sample
		conf.FastBuildBlobbed = false;
		conf.StripFastBuildSourceFiles = false;
		conf.FastBuildNoBlobStrategy = Project.Configuration.InputFileStrategy.Exclude; // Set this so that CompilerInputPath is used

		//conf.fastbuild
		//conf.AdditionalCompilerOptions.Add("-Wno-error=unused-command-line-argument");
		conf.Options.Add(Options.Vc.General.DebugInformation.ProgramDatabase);
		// workaround necessity of rc.exe
		conf.Options.Add(Options.Vc.Linker.EmbedManifest.No);
	}
}

[Fragment, Flags]
public enum Optimization
{
	Debug = 1 << 0,
	Release = 1 << 1,
	Retail = 1 << 2,
	//ToolDebug = 1 << 3,
	//ToolRelease = 1 << 4,
}

public class CustomTarget : ITarget
{
	public Optimization Optimization;

	public DevEnv DevEnv;

	public Platform Platform;

	public BuildSystem BuildSystem;

	public DotNetFramework Framework;


	public override string Name => Optimization.ToString();

	public override Sharpmake.Optimization GetOptimization()
	{
		switch (Optimization)
		{
			case Optimization.Debug: return Sharpmake.Optimization.Debug;
			case Optimization.Release: return Sharpmake.Optimization.Release;
			case Optimization.Retail: return Sharpmake.Optimization.Retail;
			//case Optimization.ToolDebug: return Sharpmake.Optimization.Debug;
			//case Optimization.ToolRelease: return Sharpmake.Optimization.Release;
			default: throw new NotSupportedException($"Optimization value {Optimization}");
		}
	}
}

[Sharpmake.Export]
public class WinPixEventRuntime : Project
{
	public WinPixEventRuntime() : base(typeof(CustomTarget))
	{
		Name = "WinPixEventRuntime";
		AddTargets(Globals.GetTargets());
	}

	[Configure]
	public void ConfigureAll(Configuration conf, CustomTarget target)
	{
		string path = Path.Combine(Globals.ThirdPartyDir, "[project.Name]");
		conf.IncludePaths.Add(Path.Combine(path, "include"));
		conf.TargetCopyFiles.Add(Path.Combine(path, "bin", "x64", "WinPixEventRuntime.dll"));
		conf.LibraryPaths.Add(Path.Combine(path, "bin", "x64"));
		conf.LibraryFiles.Add("WinPixEventRuntime.lib");
	}
}

[Sharpmake.Export]
public class D3D12AgilitySDK : Project
{
	public D3D12AgilitySDK() : base(typeof(CustomTarget))
	{
		Name = "d3d12-agility-sdk";
		AddTargets(Globals.GetTargets());
	}

	[Configure]
	public void ConfigureAll(Configuration conf, CustomTarget target)
	{
		string path = Path.Combine(Globals.ThirdPartyDir, "[project.Name]");
		conf.IncludePaths.Add(Path.Combine(path, "include"));
		conf.TargetCopyFiles.Add(Path.Combine(path, "bin", "[target.Platform]", "D3D12Core.dll"));
		conf.TargetCopyFiles.Add(Path.Combine(path, "bin", "[target.Platform]", "d3d12SDKLayers.dll"));
		conf.LibraryFiles.Add("dxgi.lib");
		conf.LibraryFiles.Add("dxguid.lib");
		conf.LibraryFiles.Add("d3d12.lib");
	}
}

[Sharpmake.Export]
public class DXC : Project
{
	public DXC() : base(typeof(CustomTarget))
	{
		Name = "dxc";
		AddTargets(Globals.GetTargets());
	}

	[Configure]
	public void ConfigureAll(Configuration conf, CustomTarget target)
	{
		string path = Path.Combine(Globals.ThirdPartyDir, "[project.Name]");
		conf.IncludePaths.Add(Path.Combine(path, "inc"));
		conf.TargetCopyFiles.Add(Path.Combine(path, "bin", "x64", "dxcompiler.dll"));
		conf.TargetCopyFiles.Add(Path.Combine(path, "bin", "x64", "dxil.dll"));
		conf.LibraryPaths.Add(Path.Combine(path, "lib", "x64"));
		conf.LibraryFiles.Add("dxcompiler.lib");
		conf.LibraryFiles.Add("dxil.lib");
	}
}

[Sharpmake.Export]
public class FreeType : Project
{
	public FreeType() : base(typeof(CustomTarget))
	{
		Name = "FreeType";
		AddTargets(Globals.GetTargets());
	}

	[Configure]
	public void ConfigureAll(Configuration conf, CustomTarget target)
	{
		string path = Path.Combine(Globals.ThirdPartyDir, "[project.Name]");
		conf.IncludePaths.Add(Path.Combine(path, "include"));
		conf.TargetCopyFiles.Add(Path.Combine(path, "bin", "[target.Platform]", "freetype.dll"));
		conf.LibraryPaths.Add(Path.Combine(path, "lib", "[target.Platform]"));
		conf.LibraryFiles.Add("freetype.lib");
	}
}

// This can't act as the full reflect project because it needs to only be references in release-mode by projects
// However if you do this, it can't then act as a normal project when you want to build it, for debugging and stuff
// Therefore we have a separate project (ReflectStandaloneProject) that has all targets and can be run with the debugger
[Sharpmake.Generate]
public class ReflectProject : Project
{

	public static CustomTarget GetDepTarget()
	{
		return new CustomTarget
		{
			DevEnv = DevEnv.vs2022,
			Platform = Platform.win64,
			Optimization = Optimization.Release,
			BuildSystem = BuildSystem.FastBuild,
			Framework = DotNetFramework.net6_0,
		};
	}

	public ReflectProject() : base(typeof(CustomTarget))
	{
		Name = "reflect";
		SourceRootPath = Path.Combine(Globals.CodeRootDir, "src", "[project.Name]");

		AddTargets(new CustomTarget
		{
			DevEnv = DevEnv.vs2022,
			Platform = Platform.win64,
			Optimization = Optimization.Release,
			BuildSystem = BuildSystem.FastBuild,
			Framework = DotNetFramework.net6_0,
		});
	}

	[Configure]
	public void ConfigureAll(Configuration conf, CustomTarget target)
	{
		conf.IntermediatePath = Path.Combine(Globals.TempDir, "code/[project.Name]/[conf.Name]/[target.Platform]");
		conf.TargetLibraryPath = "[conf.IntermediatePath]";
		// .lib files must be with the .obj files when running in fastbuild distributed mode or we'll have missing symbols due to merging of the .pdb
		conf.TargetPath = Path.Combine(Globals.BuildDir, "[target.Optimization]/[target.Platform]");
		//conf.Name = "[target.DevEnv]_[target.Optimization]_[target.Platform]_[target.Framework]";
		conf.ProjectPath = "[project.SourceRootPath]";
		conf.Output = Configuration.OutputType.Exe;
		Globals.ApplyLanguageSettings(conf, target);
		conf.StripFastBuildSourceFiles = true;
	}
}

[Sharpmake.Generate]
public class ReflectStandaloneProject : Project
{

	public ReflectStandaloneProject() : base(typeof(CustomTarget))
	{
		Name = "reflect-standalone";
		SourceRootPath = Path.Combine(Globals.CodeRootDir, "src", "reflect");

		AddTargets(Globals.GetTargets());
	}

	[Configure]
	public void ConfigureAll(Configuration conf, CustomTarget target)
	{
		conf.IntermediatePath = Path.Combine(Globals.TempDir, "code/[project.Name]/[conf.Name]/[target.Platform]");
		conf.TargetLibraryPath = "[conf.IntermediatePath]";
		// .lib files must be with the .obj files when running in fastbuild distributed mode or we'll have missing symbols due to merging of the .pdb
		conf.TargetPath = Path.Combine(Globals.BuildDir, "[target.Optimization]/[target.Platform]");
		//conf.Name = "[target.DevEnv]_[target.Optimization]_[target.Platform]_[target.Framework]";
		conf.ProjectPath = "[project.SourceRootPath]";
		conf.Output = Configuration.OutputType.Exe;
		Globals.ApplyLanguageSettings(conf, target);
	}
}

public class PawProject : Project
{
	private List<string> files_to_reflect = new List<string>();
	private string code_generation_path;

	public PawProject() : base(typeof(CustomTarget))
	{
		AddTargets(Globals.GetTargets());
		CustomProperties.Add("ShowAllFiles", "true");
		SourceRootPath = Path.Combine(Globals.CodeRootDir, "src", "[project.Name]");
		//SourceFilesExcludeRegex.Add("\\.*\\.(generated)\\.cpp");
		SourceFilesCPPExtensions.Add(".c", ".inl");
		code_generation_path = Path.Combine(Globals.TempDir, "code", "[project.Name]", "generated");
		//AdditionalSourceRootPaths.Add(code_generation_path);
		// filter out generated files in vs project file (not fastbuild)
		//SourceFilesExcludeRegex.Add(@"[/\\]generated[/\\][^/\\]+");
	}

	protected override void ExcludeOutputFiles()
	{
		base.ExcludeOutputFiles();
		string code_src_dir = Globals.CodeSrcDir.Replace("C:\\", "c:\\");
		foreach (var item in ResolvedSourceFiles)
		{
			if (item.StartsWith(code_src_dir) && (Path.GetExtension(item) == ".h"))
			{
				files_to_reflect.Add(item);
				//Console.WriteLine($"Accepted {item}");
			}
			//else
			//{
			//	Console.WriteLine($"Rejected {item}");
			//}
			//Console.WriteLine($"File: {item} -- {item.EndsWith(".generated.cpp")}");
		}

		//Console.WriteLine(String.Join(", ", files_to_reflect));
	}

	// Strings have been resolved
	public override void PostLink()
	{
		base.PostLink();

		string generated_path = Path.Combine(SourceRootPath, "_generated");
		if (Directory.Exists(generated_path))
		{
			Directory.Delete(generated_path, true);
		}
		Directory.CreateDirectory(generated_path);

		//Console.WriteLine(compile_argument_map);
		string output_cpp_file = Path.Combine(generated_path, "type_info.cpp");
		string output_header = Path.Combine(generated_path, "type_info.h");
		string arguments = $"%2 \"{output_header}\" %1 ";
		Strings input_files = new Strings();
		foreach (var file in files_to_reflect)
		{
			string trimmed_file = Path.GetFileNameWithoutExtension(file);
			string new_extension;
			string old_extension = Path.GetExtension(file);
			switch (old_extension)
			{
				case ".h": new_extension = ".generated_h.cpp"; break;
				case ".hpp": new_extension = ".generated_hpp.cpp"; break;
				case ".cpp": new_extension = ".generated_cpp.cpp"; break;
				default:
					throw new Exception($"Unsupported extension ({old_extension}) for reflection");
			}
			string output_file = Path.Combine(generated_path, trimmed_file + new_extension);

			//output_file = Path.ChangeExtension(output_file, new_extension);

			////Console.WriteLine(output_file);
			//conf.CustomFileBuildSteps.Add(new Configuration.CustomFileBuildStep()
			//{
			//	KeyInput = file,
			//	Output = output_file,
			//	Executable = Path.Combine(Globals.BuildDir, "release", conf.Target.GetPlatform().ToString(), "reflect.exe"),
			//	Description = $"reflect {file}",
			//	ExecutableArguments = $"\"{file}\" \"{output_file}\""
			//});

			input_files.Add(file);

			//arguments += $"\"{file}\" \"{output_file}\" ";

		}

		//Console.WriteLine(arguments);

		//foreach (Configuration conf in Configurations)
		//{


		//	var pre_build_step = new Configuration.BuildStepExecutable(
		//		Path.Combine(Globals.BuildDir, "release", conf.Target.GetPlatform().ToString(), "reflect.exe"),
		//		"",
		//		output_cpp_file, ""
		//		);
		//	pre_build_step.FastBuildExecutableInputFiles = input_files;
		//	pre_build_step.ExecutableOtherArguments = arguments;

		//	conf.EventCustomPrebuildExecute.Add($"reflect-{Name}", pre_build_step);
		//}
	}

	public virtual void ConfigureAll(Project.Configuration conf, CustomTarget target)
	{
		conf.IntermediatePath = Path.Combine(Globals.TempDir, "code/[project.Name]/[conf.Name]/[target.Platform]");
		conf.TargetLibraryPath = "[conf.IntermediatePath]";
		// .lib files must be with the .obj files when running in fastbuild distributed mode or we'll have missing symbols due to merging of the .pdb
		conf.TargetPath = Path.Combine(Globals.BuildDir, "[target.Optimization]", "[target.Platform]");
		//conf.Name = "[target.DevEnv]_[target.Optimization]_[target.Platform]_[target.Framework]";
		conf.ProjectPath = "[project.SourceRootPath]";

		if (target.Platform == Platform.win64)
		{
			conf.Defines.Add("NOMINMAX");
		}

		conf.Defines.Add($"PAW_TEST_PROJECT_NAME={Name}");


		switch (target.Optimization)
		{
			case Optimization.Debug:
			{
				conf.Defines.Add("PAW_TESTS");
				conf.Defines.Add("PAW_DEBUG");
			}
			break;

			case Optimization.Release:
			{
				conf.Defines.Add("PAW_TESTS");
				conf.Defines.Add("PAW_RELEASE");
			}
			break;

			case Optimization.Retail:
			{
				conf.Defines.Add("PAW_RETAIL");
			}
			break;
		}

		Globals.ApplyLanguageSettings(conf, target);

		if (conf.VcxprojUserFile == null)
		{
			conf.VcxprojUserFile = new Configuration.VcxprojUserFileSettings();
			conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = Globals.RootDir;
		}

		conf.AddPrivateDependency<ReflectProject>(ReflectProject.GetDepTarget(), DependencySetting.OnlyBuildOrder);

		//conf.IncludePrivatePaths.Add(code_generation_path);
		//conf.IncludePrivatePaths.Add(Path.Combine("[project.SourceRootPath]", "_generated"));


	}
}

public class TestsProjectBase : PawProject
{
	public TestsProjectBase() : base()
	{

	}

	[Configure]
	public override void ConfigureAll(Project.Configuration conf, CustomTarget target)
	{
		base.ConfigureAll(conf, target);
		conf.Options.Add(Options.Vc.Linker.SubSystem.Console);

		conf.Output = Configuration.OutputType.Exe;
		conf.AddPrivateDependency<TestingProject>(target);

	}
}

[Generate]
public class ShaderProject : Project
{
	public ShaderProject() : base(typeof(CustomTarget))
	{
		Name = "shaders";
		AddTargets(Globals.GetTargets());
		SourceRootPath = Path.Combine(Globals.RootDir, "source-data", "shaders");
		SourceFilesExtensions.Add(".hlsl");
	}

	[Configure]
	public virtual void ConfigureAll(Project.Configuration conf, CustomTarget target)
	{
		//conf.Name = "[target.DevEnv]_[target.Optimization]_[target.Platform]_[target.Framework]";
		conf.IntermediatePath = Path.Combine(Globals.TempDir, "code/[project.Name]/[conf.Name]/[target.Platform]");
		conf.TargetPath = Path.Combine(Globals.BuildDir, "[target.Optimization]/[target.Platform]");
		// conf.ProjectPath = Globals.ProjectFilesDir;
		conf.ProjectPath = "[project.SourceRootPath]";
		conf.Output = Configuration.OutputType.Utility;
	}
}

[Generate]
public class CoreTestsProject : TestsProjectBase
{
	public CoreTestsProject() : base()
	{
		Name = "core-tests";
	}
}


[Generate]
public class TestingProject : PawProject
{
	public TestingProject() : base()
	{
		Name = "testing";
	}

	[Configure]
	public override void ConfigureAll(Project.Configuration conf, CustomTarget target)
	{
		base.ConfigureAll(conf, target);

		conf.Output = Configuration.OutputType.Lib;
		conf.AddPublicDependency<CoreProject>(target);

		List<string> excluded_file_suffixes = new List<string>();

		Platform[] all_platforms = Enum.GetValues<Platform>();
		foreach (Platform platform in all_platforms)
		{
			if (target.Platform != platform)
			{
				excluded_file_suffixes.Add(platform.ToString());
			}
		}

		conf.SourceFilesBuildExcludeRegex.Add(@"\.*_(" + string.Join("|", excluded_file_suffixes.ToArray()) + @")\.c$");


		conf.IncludePaths.Add("[project.SourceRootPath]/public");

		conf.LibraryFiles.Add("onecore.lib");

	}
}

[Generate]
public class CoreProject : PawProject
{
	public CoreProject() : base()
	{
		Name = "core";
	}

	[Configure]
	public override void ConfigureAll(Project.Configuration conf, CustomTarget target)
	{
		base.ConfigureAll(conf, target);

		conf.Output = Configuration.OutputType.Lib;

		List<string> excluded_file_suffixes = new List<string>();

		Platform[] all_platforms = Enum.GetValues<Platform>();
		foreach (Platform platform in all_platforms)
		{
			if (target.Platform != platform)
			{
				excluded_file_suffixes.Add(platform.ToString());
			}
		}

		conf.SourceFilesBuildExcludeRegex.Add(@"\.*_(" + string.Join("|", excluded_file_suffixes.ToArray()) + @")\.c$");


		conf.IncludePaths.Add("[project.SourceRootPath]/public");


		conf.LibraryFiles.Add("onecore.lib");
		conf.LibraryFiles.Add("user32.lib");

		conf.AddPrivateDependency<D3D12AgilitySDK>(target);
		conf.AddPrivateDependency<DXC>(target);
		conf.AddPrivateDependency<WinPixEventRuntime>(target);

		conf.AdditionalCompilerOptions.Add("-Wno-language-extension-token"); // __uuidof

	}
}

[Generate]
public class PresentationProject : PawProject
{
	public PresentationProject() : base()
	{
		Name = "presentation";
	}

	[Configure]
	public override void ConfigureAll(Project.Configuration conf, CustomTarget target)
	{
		base.ConfigureAll(conf, target);

		conf.Options.Add(Options.Vc.Linker.SubSystem.Console);

		conf.Output = Configuration.OutputType.Exe;
		conf.AddPrivateDependency<CoreProject>(target);
		conf.AddPrivateDependency<FreeType>(target);

	}
}

[Generate]
public class PawSolution : Solution
{
	public PawSolution() : base(typeof(CustomTarget))
	{
		Name = "paw";

		AddTargets(Globals.GetTargets());
		GenerateFastBuildAllProject = false;
	}

	[Configure]
	public void ConfigureAll(Solution.Configuration conf, CustomTarget target)
	{
		conf.SolutionPath = "[solution.SharpmakeCsPath]";
		conf.PlatformName = target.Platform.ToString();
		conf.AddProject<ReflectStandaloneProject>(target);
		conf.AddProject<CoreTestsProject>(target);
		conf.AddProject<PresentationProject>(target);
		conf.AddProject<ShaderProject>(target);
	}
}

public static class Main
{
	[Sharpmake.Main]
	public static void SharpmakeMain(Sharpmake.Arguments arguments)
	{
		arguments.Builder.EventPostSolutionLink += GenerateSolutionDatabase;

		Globals.InitPaths();

		// This will stop fastbuild from erroring because there's already an instance running and instead wait for it to finish.
		// This is useful when visual studio does stuff like solution build, where it will try build all the projects without waiting
		FastBuildSettings.FastBuildWait = true;

		string sharpmakeFastBuildDir = Path.Combine(Globals.RootDir, "utils", "fastbuild");
		FastBuildSettings.FastBuildMakeCommand = Path.Combine(sharpmakeFastBuildDir, "FBuild.exe");

		// This is necessary since there is no rc.exe in the same directory than link.exe
		FastBuildSettings.SetPathToResourceCompilerInEnvironment = true;

		KitsRootPaths.SetUseKitsRootForDevEnv(DevEnv.vs2022, KitsRootEnum.KitsRoot10,
			Options.Vc.General.WindowsTargetPlatformVersion.v10_0_22621_0);
		arguments.Generate<PawSolution>();
	}

	private static void GenerateSolutionDatabase(Solution solution)
	{
		var configs = solution.Configurations.SelectMany(c => c.IncludedProjectInfos.Select(pi => pi.Configuration));
		GenerateDatabase(Globals.CodeRootDir, configs, CompileCommandFormat.Command);
	}

	private static void GenerateDatabase(string outdir, IEnumerable<Project.Configuration> configs, CompileCommandFormat format)
	{
		var builder = Builder.Instance;

		if (builder == null)
		{
			System.Console.Error.WriteLine("CompilationDatabase: No builder instance.");
			return;
		}

		var generator = new JsonCompilationDatabase();

		generator.Generate(builder, outdir, configs, format, new List<string>(), new List<string>());
	}
}