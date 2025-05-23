const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

const DEFAULT_THEME_ID = "default-theme@mozilla.org";

let global = this;

// Test that paths in the extensions database are stored properly
// if they include non-ascii characters (see bug 1428234 for an example of
// a past bug with such paths)
add_task(async function test_non_ascii_path() {
  // Note: We can only change the profile directory location if
  // do_get_profile() has not been called yet. On Android, this does not work
  // because do_get_profile() is called before the test starts:
  // https://searchfox.org/mozilla-central/rev/45d158e24489a6bad5a2d2d384b5083595c6c29c/testing/xpcshell/head.js#516-519
  const PROFILE_VAR = "XPCSHELL_TEST_PROFILE_DIR";
  let profileDir = PathUtils.join(
    Services.env.get(PROFILE_VAR),
    "\u00ce \u00e5m \u00f1\u00f8t \u00e5s\u00e7ii"
  );
  Services.env.set(PROFILE_VAR, profileDir);

  AddonTestUtils.init(global);
  AddonTestUtils.overrideCertDB();
  AddonTestUtils.createAppInfo(
    "xpcshell@tests.mozilla.org",
    "XPCShell",
    "1",
    "1"
  );

  const ID1 = "profile1@tests.mozilla.org";
  let xpi1 = await AddonTestUtils.createTempWebExtensionFile({
    id: ID1,
    manifest: {
      browser_specific_settings: { gecko: { id: ID1 } },
    },
  });

  const ID2 = "profile2@tests.mozilla.org";
  let xpi2 = await AddonTestUtils.createTempWebExtensionFile({
    id: ID2,
    manifest: {
      browser_specific_settings: { gecko: { id: ID2 } },
    },
  });

  await AddonTestUtils.manuallyInstall(xpi1);
  await AddonTestUtils.promiseStartupManager();
  await AddonTestUtils.promiseInstallFile(xpi2);
  await AddonTestUtils.promiseShutdownManager();

  let dbfile = PathUtils.join(profileDir, "extensions.json");
  let data = await IOUtils.readJSON(dbfile);

  let addons = data.addons.filter(a => a.id !== DEFAULT_THEME_ID);
  Assert.ok(Array.isArray(addons), "extensions.json has addons array");
  Assert.equal(2, addons.length, "extensions.json has 2 addons");
  Assert.ok(
    addons[0].path.startsWith(profileDir),
    "path property for sideloaded extension has the proper profile directory"
  );
  Assert.ok(
    addons[1].path.startsWith(profileDir),
    "path property for extension installed at runtime has the proper profile directory"
  );
});
