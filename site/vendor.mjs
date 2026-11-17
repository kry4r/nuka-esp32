// SPDX-License-Identifier: AGPL-3.0-or-later
import {build} from "esbuild";
import {cp, mkdir, readFile, writeFile} from "node:fs/promises";
import {dirname, join} from "node:path";

// ESP Web Tools' new-install default erases the whole chip. nuka forbids that.
await build({
  entryPoints: ["node_modules/esp-web-tools/dist/web/install-button.js"],
  bundle: true, format: "esm", target: "es2022", outdir: "vendor", splitting: true,
  legalComments: "eof", minify: true,
  plugins: [{name: "nuka-preserve-flash", setup(builder) {
    builder.onLoad({filter: /esp-web-tools[\\/]dist[\\/]web[\\/]install-dialog-[\w-]+\.js$/}, async ({path}) => {
      const source = await readFile(path, "utf8");
      const assignment = 'this._state="INSTALL",this._installErase=e,this._installConfirmed=!1';
      if (source.split(assignment).length !== 2) throw new Error("Review the no-full-erase patch for this ESP Web Tools version");
      return {contents: source.replace(assignment, 'this._state="INSTALL",this._installErase=!1,this._installConfirmed=!1'), loader: "js", resolveDir: dirname(path)};
    });
  }}],
});
const lock = JSON.parse(await readFile("package-lock.json", "utf8"));
const notices = [];
for (const [path, metadata] of Object.entries(lock.packages)) {
  if (!path || path.includes("@esbuild/")) continue;
  const name = path.slice("node_modules/".length);
  const files = await import("node:fs/promises").then((fs) => fs.readdir(path));
  const licenseFiles = files.filter((file) => /^(license|copying|notice)(\.|$)/i.test(file));
  await mkdir(join("vendor/licenses", name), {recursive: true});
  for (const file of licenseFiles) await cp(join(path, file), join("vendor/licenses", name, file));
  // This package's npm files list omits Lit's repository-level BSD license.
  if (name === "@lit-labs/ssr-dom-shim") await cp("licenses/ssr-dom-shim-LICENSE", join("vendor/licenses", name, "LICENSE"));
  notices.push(`${name} ${metadata.version}: ${metadata.license}`);
}
await writeFile("vendor/DEPENDENCIES.txt", notices.join("\n") + "\n");
