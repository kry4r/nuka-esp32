// SPDX-License-Identifier: AGPL-3.0-or-later
import "./vendor/install-button.js";
const checkbox = document.querySelector("#backed-up");
checkbox.addEventListener("change", () => {
  document.querySelector("#installer").hidden = !checkbox.checked;
  document.querySelector("#backup-required").hidden = checkbox.checked;
});
