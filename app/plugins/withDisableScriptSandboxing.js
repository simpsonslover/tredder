const { withXcodeProject } = require('@expo/config-plugins');

/**
 * Sets ENABLE_USER_SCRIPT_SANDBOXING = NO on every build configuration in the
 * generated Xcode project.
 *
 * Without this, Xcode's script-phase sandbox blocks the React Native bundler
 * from writing main.jsbundle into the build directory, causing an EPERM error
 * in the "Bundle React Native code and images" phase and a failed archive.
 *
 * The setting resets to YES whenever `expo prebuild --clean` regenerates ios/,
 * so it must live here rather than be set by hand in Xcode.
 */
module.exports = function withDisableScriptSandboxing(config) {
  return withXcodeProject(config, (config) => {
    const proj = config.modResults;
    const configurations = proj.pbxXCBuildConfigurationSection();
    for (const [, buildConfig] of Object.entries(configurations)) {
      if (buildConfig && typeof buildConfig === 'object' && buildConfig.buildSettings) {
        buildConfig.buildSettings.ENABLE_USER_SCRIPT_SANDBOXING = 'NO';
      }
    }
    return config;
  });
};
