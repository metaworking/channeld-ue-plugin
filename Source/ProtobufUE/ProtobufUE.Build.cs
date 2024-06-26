using UnrealBuildTool;
using System.IO;

public class ProtobufUE : ModuleRules
{
	public ProtobufUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });

		PublicSystemIncludePaths.AddRange(new string[] { Path.Combine(ModuleDirectory, "ThirdParty", "include") });

		if (Target.bForceEnableRTTI)
		{
			bUseRTTI = true;
			PublicDefinitions.Add("GOOGLE_PROTOBUF_NO_RTTI=0");
		}
		else
		{
			bUseRTTI = false;
			PublicDefinitions.Add("GOOGLE_PROTOBUF_NO_RTTI=1");
		}

		PublicDefinitions.AddRange(
			new string[]
			{
				"LANG_CXX11",
				"PROTOBUF_USE_DLLS", // Basically for ThreadCache
				"HAVE_ZLIB=0",
				"GOOGLE_PROTOBUF_INTERNAL_DONATE_STEAL=0",
			}
		);

		ShadowVariableWarningLevel = WarningLevel.Off;
		bEnableUndefinedIdentifierWarnings = false;
	}
}