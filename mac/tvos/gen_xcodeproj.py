#!/usr/bin/env python3
"""Writes Tempest2000TV.xcodeproj/project.pbxproj (kept as a script so the project is reproducible)."""
import os, sys
TEAM = sys.argv[1] if len(sys.argv) > 1 else "YCC9568JU7"
BUNDLE = sys.argv[2] if len(sys.argv) > 2 else "com.jnaina.tempest2000.tv"
def i(n): return "T2K%021X" % n
F_MAIN, F_ABS, F_ASSETS, F_PLIST, F_PROD = 1, 2, 3, 4, 5
B_MAIN, B_ABS, B_ASSETS = 11, 12, 13
G_ROOT, G_SRC, G_PRODS = 20, 21, 22
T, P, SRC_PH, RES_PH, FW_PH = 30, 31, 32, 33, 34
CL_P, CL_T, C_PD, C_PR, C_TD, C_TR = 40, 41, 42, 43, 44, 45
common = f'''
				SDKROOT = appletvos;
				TARGETED_DEVICE_FAMILY = 3;
				TVOS_DEPLOYMENT_TARGET = 15.0;
				CLANG_ENABLE_OBJC_ARC = YES;
				CLANG_ENABLE_MODULES = YES;'''
pbx = f'''// !$*UTF8*$!
{{
	archiveVersion = 1; classes = {{}}; objectVersion = 56;
	objects = {{
/* Begin PBXBuildFile section */
		{i(B_MAIN)} = {{isa = PBXBuildFile; fileRef = {i(F_MAIN)}; }};
		{i(B_ABS)} = {{isa = PBXBuildFile; fileRef = {i(F_ABS)}; }};
		{i(B_ASSETS)} = {{isa = PBXBuildFile; fileRef = {i(F_ASSETS)}; }};
/* End PBXBuildFile section */
/* Begin PBXFileReference section */
		{i(F_MAIN)} = {{isa = PBXFileReference; lastKnownFileType = sourcecode.c.objc; path = main.m; sourceTree = "<group>"; }};
		{i(F_ABS)} = {{isa = PBXFileReference; lastKnownFileType = file; path = t2000.abs; sourceTree = "<group>"; }};
		{i(F_ASSETS)} = {{isa = PBXFileReference; lastKnownFileType = folder.assetcatalog; path = Assets.xcassets; sourceTree = "<group>"; }};
		{i(F_PLIST)} = {{isa = PBXFileReference; lastKnownFileType = text.plist.xml; path = Info.plist; sourceTree = "<group>"; }};
		{i(F_PROD)} = {{isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = "Tempest 2000.app"; sourceTree = BUILT_PRODUCTS_DIR; }};
/* End PBXFileReference section */
/* Begin PBXFrameworksBuildPhase section */
		{i(FW_PH)} = {{isa = PBXFrameworksBuildPhase; buildActionMask = 2147483647; files = (); runOnlyForDeploymentPostprocessing = 0; }};
/* End PBXFrameworksBuildPhase section */
/* Begin PBXGroup section */
		{i(G_ROOT)} = {{isa = PBXGroup; children = ({i(G_SRC)}, {i(G_PRODS)}); sourceTree = "<group>"; }};
		{i(G_SRC)} = {{isa = PBXGroup; children = ({i(F_MAIN)}, {i(F_PLIST)}, {i(F_ABS)}, {i(F_ASSETS)}); path = T2KTV; sourceTree = "<group>"; }};
		{i(G_PRODS)} = {{isa = PBXGroup; children = ({i(F_PROD)}); name = Products; sourceTree = "<group>"; }};
/* End PBXGroup section */
/* Begin PBXNativeTarget section */
		{i(T)} = {{
			isa = PBXNativeTarget; buildConfigurationList = {i(CL_T)};
			buildPhases = ({i(SRC_PH)}, {i(FW_PH)}, {i(RES_PH)});
			buildRules = (); dependencies = (); name = Tempest2000TV; productName = Tempest2000TV;
			productReference = {i(F_PROD)}; productType = "com.apple.product-type.application";
		}};
/* End PBXNativeTarget section */
/* Begin PBXProject section */
		{i(P)} = {{
			isa = PBXProject; buildConfigurationList = {i(CL_P)}; compatibilityVersion = "Xcode 14.0";
			developmentRegion = en; hasScannedForEncodings = 0; knownRegions = (en, Base);
			mainGroup = {i(G_ROOT)}; productRefGroup = {i(G_PRODS)}; projectDirPath = ""; projectRoot = ""; targets = ({i(T)});
		}};
/* End PBXProject section */
/* Begin PBXResourcesBuildPhase section */
		{i(RES_PH)} = {{isa = PBXResourcesBuildPhase; buildActionMask = 2147483647; files = ({i(B_ABS)}, {i(B_ASSETS)}); runOnlyForDeploymentPostprocessing = 0; }};
/* End PBXResourcesBuildPhase section */
/* Begin PBXSourcesBuildPhase section */
		{i(SRC_PH)} = {{isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = ({i(B_MAIN)}); runOnlyForDeploymentPostprocessing = 0; }};
/* End PBXSourcesBuildPhase section */
/* Begin XCBuildConfiguration section */
		{i(C_PD)} = {{isa = XCBuildConfiguration; name = Debug; buildSettings = {{{common}
				ONLY_ACTIVE_ARCH = YES; }}; }};
		{i(C_PR)} = {{isa = XCBuildConfiguration; name = Release; buildSettings = {{{common}
				GCC_OPTIMIZATION_LEVEL = s; }}; }};
'''
tset = f'''
				PRODUCT_NAME = "Tempest 2000";
				PRODUCT_BUNDLE_IDENTIFIER = {BUNDLE};
				INFOPLIST_FILE = T2KTV/Info.plist;
				ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;
				CODE_SIGN_STYLE = Automatic;
				DEVELOPMENT_TEAM = {TEAM};
				HEADER_SEARCH_PATHS = "$(SRCROOT)/..";
				"OTHER_LDFLAGS[sdk=appletvos*]" = "$(SRCROOT)/libvjcore_dev.a";
				"OTHER_LDFLAGS[sdk=appletvsimulator*]" = "$(SRCROOT)/libvjcore_sim.a";
				GENERATE_INFOPLIST_FILE = NO;
'''
for cid, nm in ((C_TD, "Debug"), (C_TR, "Release")):
    pbx += f"\t\t{i(cid)} = {{isa = XCBuildConfiguration; name = {nm}; buildSettings = {{{tset}\t\t\t\t}}; }};\n"
pbx += f'''/* End XCBuildConfiguration section */
/* Begin XCConfigurationList section */
		{i(CL_P)} = {{isa = XCConfigurationList; buildConfigurations = ({i(C_PD)}, {i(C_PR)}); defaultConfigurationIsVisible = 0; defaultConfigurationName = Release; }};
		{i(CL_T)} = {{isa = XCConfigurationList; buildConfigurations = ({i(C_TD)}, {i(C_TR)}); defaultConfigurationIsVisible = 0; defaultConfigurationName = Release; }};
/* End XCConfigurationList section */
	}};
	rootObject = {i(P)};
}}
'''
d = "Tempest2000TV.xcodeproj"; os.makedirs(d, exist_ok=True); open(f"{d}/project.pbxproj", "w").write(pbx)
