// ==UserScript==
// @name        GM bridge test
// @match       https://example.com/*
// @match       http://127.0.0.1/*
// @run-at      document-end
// @grant       GM.setValue
// @grant       GM.getValue
// @grant       GM.xmlHttpRequest
// ==/UserScript==

// Shows one yellow bar with the result of each GM call. Responses are
// reported by status and length only, never by their content.
(async function () {
	var bar = document.createElement("div");
	bar.id = "gm-bridge-test";
	bar.style.cssText = "position:fixed;top:0;left:0;right:0;z-index:99999;"
		+ "background:#ff0;color:#000;font:20px sans-serif;padding:10px";
	document.documentElement.appendChild(bar);
	var results = [];
	function show(name, text) {
		results.push(name + "=" + text);
		bar.textContent = "GM: " + results.join("  ");
	}
	function fetchResult(r) {
		if (!r.ok)
			return r.error || ("failed, status " + r.status);
		return r.status + " (" + String(r.text || "").length + " bytes)";
	}
	try {
		var set = await GM.setValue("probe", "stored");
		show("set", set.ok ? "ok" : "failed");
		show("get", await GM.getValue("probe", "missing"));
		show("file", fetchResult(await GM.xmlHttpRequest(
			{ url: "file:///etc/hostname" })));
		show("loopback", fetchResult(await GM.xmlHttpRequest(
			{ url: "http://127.0.0.1:22/" })));
		show("lan", fetchResult(await GM.xmlHttpRequest(
			{ url: "http://192.168.1.1/" })));
		show("web", fetchResult(await GM.xmlHttpRequest(
			{ url: "https://www.example.org/", method: "GET" })));
	} catch (e) {
		show("threw", String(e));
	}
})();
