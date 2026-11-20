// Host-side test of the Store page template: stubs the DOM and the
// webkit message handler, then drives Store.updateState() the way the
// C++ side does and asserts the rendered result.
"use strict";
const fs = require("fs");
const path = require("path");

const dataDir = path.join(__dirname, "..", "..", "..", "apps", "store", "data");
const html = fs.readFileSync(path.join(dataDir, "index.html"), "utf8");
const js = fs.readFileSync(path.join(dataDir, "store.js"), "utf8");
const css = fs.readFileSync(path.join(dataDir, "store.css"), "utf8");

// --- minimal DOM stub ---
function makeElement(tag) {
	const el = {
		tagName: tag.toUpperCase(),
		className: "",
		_textContent: "",
		src: "",
		alt: "",
		style: {},
		disabled: false,
		children: [],
		parentNode: null,
		listeners: {},
		appendChild(child) { this.children.push(child); child.parentNode = this; return child; },
		addEventListener(ev, fn) { (this.listeners[ev] = this.listeners[ev] || []).push(fn); },
		replaceChild(n, old) {
			const i = this.children.indexOf(old);
			this.children[i] = n; n.parentNode = this;
		},
		click() {
			(this.listeners.click || []).forEach(fn => fn());
			if (typeof this.onclick === "function") this.onclick();
		},
	};
	// Real DOM semantics: assigning textContent wipes children.
	Object.defineProperty(el, "textContent", {
		get() { return this._textContent; },
		set(v) { this._textContent = v; this.children = []; }
	});
	return el;
}

const ids = {};
["categories", "sections", "entries", "detail", "detail-previews",
 "detail-icon", "detail-letter", "detail-name", "detail-summary",
 "detail-requires", "detail-meta", "detail-back", "content", "search", "detail-action", "progress", "progress-bar",
 "progress-text", "status", "more", "app", "sidebar", "content"]
	.forEach(id => { ids[id] = makeElement("div"); });

const posted = [];
global.window = {
	webkit: { messageHandlers: { store: { postMessage: (m) => posted.push(m) } } }
};
global.document = {
	_ids: ids,
	getElementById(id) { return ids[id]; },
	createElement(tag) { return makeElement(tag); },
	addEventListener(ev, fn) { if (ev === "DOMContentLoaded") this._ready = fn; },
	_ready: null
};

// Fake timers so the search debounce can be driven.
const timers = [];
global.setTimeout = (fn, ms) => { timers.push({fn, ms}); return timers.length; };
global.clearTimeout = (id) => { if (timers[id - 1]) timers[id - 1].fn = null; };
const fire = () => { timers.splice(0).forEach(t => t.fn && t.fn()); };

// Execute the page script.
eval(js);

// Fire DOMContentLoaded (posts "ready").
document._ready();
if (posted[0] !== "ready")
	throw new Error("expected ready post, got " + JSON.stringify(posted));

// Push state the way C++ does: the apps section.
const state = {
	sections: ["app", "component", "data"],
	section: "app",
	categories: ["All", "Internet", "Graphics"],
	selected: "All",
	busy: null,
	entries: [
		{id: "netplus", kind: "app", name: "NetPlus", summary: "Browser",
			category: "Internet", installed: false, requires: "", icon: ""},
		{id: "gimp", kind: "app", name: "GIMP", summary: "Images",
			category: "Graphics", installed: true, requires: "", icon: ""}
	]
};
window.Store.updateState(state);

// Three section tabs rendered, Apps selected.
if (ids.sections.children.length !== 3)
	throw new Error("expected 3 sections, got " + ids.sections.children.length);
if (ids.sections.children[0].className !== "selected")
	throw new Error("Apps section should be selected");
if (ids.sections.children[0].textContent !== "Apps")
	throw new Error("first tab should read Apps");

// Categories rendered in the apps section.
if (ids.categories.className === "hidden")
	throw new Error("categories should be visible in the apps section");
if (ids.categories.children.length !== 3)
	throw new Error("expected 3 categories, got " + ids.categories.children.length);

// Both entries listed.
if (ids.entries.children.length !== 2)
	throw new Error("expected 2 entries, got " + ids.entries.children.length);

// Click the first entry -> detail shown with Install.
ids.entries.children[0].click();
if (ids.detail.className === "hidden")
	throw new Error("detail should be visible after click");
if (ids["detail-name"].textContent !== "NetPlus")
	throw new Error("detail name wrong: " + ids["detail-name"].textContent);
if (ids["detail-action"].textContent !== "Install")
	throw new Error("expected Install button");
if (posted.indexOf("info|netplus") < 0)
	throw new Error("click should post info|netplus: " + JSON.stringify(posted));

// Detail meta shows category, packages and installed state.
window.Store.updateState(Object.assign({}, state, {info: "netplus",
	entries: [Object.assign({}, state.entries[0],
		{packages: ["netplus", "wpe"]}), state.entries[1]]}));
{
	const meta = ids["detail-meta"].textContent;
	if (meta.indexOf("Category: Internet") < 0 || meta.indexOf("netplus, wpe") < 0
			|| meta.indexOf("Not installed") < 0)
		throw new Error("detail meta wrong: " + meta);
}

// Search is native: debounced post("search|q"), no local filtering.
{
	const s = ids.search;
	const type = v => { s.value = v; s.listeners.input.forEach(f => f.call(s)); };
	type("G"); type("GIM");
	if (posted.indexOf("search|gim") >= 0) throw new Error("search must be debounced");
	fire();
	if (posted.filter(p => p.indexOf("search|") === 0).join() !== "search|gim")
		throw new Error("expected a single search|gim: " + JSON.stringify(posted));
	if (ids.entries.children.length !== 2) throw new Error("page does not filter locally");
	type("");
	fire();
	if (posted.indexOf("search|") < 0 && posted.indexOf("search") < 0)
		throw new Error("empty search must be posted");
}

// Paging: total > entries shows Show more, scroll/click posts once.
{
	window.Store.updateState(Object.assign({}, state, {total: 100}));
	if (ids.more.className === "hidden") throw new Error("Show more should be visible");
	ids.more.click();
	ids.more.click();
	if (posted.filter(p => p === "more").length !== 1)
		throw new Error("more must be requested once per page");
	// A longer list appends without rebuilding the head.
	const head = ids.entries.children[0];
	window.Store.updateState(Object.assign({}, state, {total: 100,
		entries: state.entries.concat([{id: "x", kind: "app", name: "X",
			summary: "", category: "Internet", installed: false,
			requires: "", icon: ""}])}));
	if (ids.entries.children.length !== 3 || ids.entries.children[0] !== head)
		throw new Error("append must keep existing nodes");
	// A changed flag replaces just that node.
	const second = ids.entries.children[1];
	window.Store.updateState(Object.assign({}, state, {total: 3,
		entries: [state.entries[0], Object.assign({}, state.entries[1],
			{installed: false}), ids.entries.children.length ? {id: "x", kind: "app",
			name: "X", summary: "", category: "Internet", installed: false,
			requires: "", icon: ""} : null]}));
	if (ids.entries.children[0] !== head || ids.entries.children[1] === second)
		throw new Error("only the changed entry is rebuilt");
	if (ids.more.className !== "hidden") throw new Error("Show more hides at the end");
	window.Store.updateState(state);
}

// Detail comes from the separate detail field when off the page.
window.Store.updateState(Object.assign({}, state, {info: "far",
	detail: {id: "far", kind: "app", name: "Far", summary: "s",
		category: "Internet", installed: true, requires: "", icon: ""}}));
if (ids["detail-name"].textContent !== "Far")
	throw new Error("detail entry not used");
window.Store.updateState(state);
ids["detail-back"].click();

window.Store.updateState(Object.assign({}, state, {info: "netplus"}));

// Press Install -> message posted.
ids["detail-action"].click();
if (posted.indexOf("install|netplus") < 0)
	throw new Error("expected install|netplus, posts: " + JSON.stringify(posted));

// Switch to the Data section: flat list, previews in the detail.
window.Store.updateState({
	sections: ["app", "component", "data"],
	section: "data",
	categories: ["All"],
	selected: "All",
	busy: null,
	entries: [
		{id: "wallpapers", kind: "data", name: "Background images",
			summary: "Wallpapers", category: "Personalization",
			installed: false, requires: "", icon: "",
			previews: ["data:image/png;base64,AAAA", "data:image/png;base64,BBBB"]}
	]
});
if (ids.sections.children[2].className !== "selected")
	throw new Error("Data section should be selected");
if (ids.categories.className !== "hidden")
	throw new Error("categories should be hidden outside the apps section");
if (ids.entries.children.length !== 1)
	throw new Error("Data filter should show 1 entry, got "
		+ ids.entries.children.length);
ids.entries.children[0].click();
if (ids["detail-previews"].className === "hidden")
	throw new Error("previews strip should be visible for artwork");
if (ids["detail-previews"].children.length !== 2)
	throw new Error("expected 2 previews, got "
		+ ids["detail-previews"].children.length);
if (ids["detail-previews"].children[0].src
		!== "data:image/png;base64,AAAA") {
	throw new Error("preview src wrong: "
		+ ids["detail-previews"].children[0].src);
}

// Busy state drives the progress bar.
window.Store.updateState({
	sections: ["app", "component", "data"],
	section: "app",
	categories: ["All", "Internet"],
	selected: "All",
	busy: {id: "netplus", percent: 42, status: "Installing"},
	entries: [
		{id: "netplus", kind: "app", name: "NetPlus", summary: "Browser",
			category: "Internet", installed: false, requires: "", icon: ""}
	]
});
ids.entries.children[0].click();
if (ids.progress.className === "hidden")
	throw new Error("progress should be visible for busy entry");
if (ids["progress-bar"].style.width !== "42%")
	throw new Error("progress width wrong: " + ids["progress-bar"].style.width);

// onProgress updates the bar.
window.Store.onProgress(80, "Almost done");
if (ids["progress-bar"].style.width !== "80%")
	throw new Error("progress not updated");

// Installed entry shows Remove.
window.Store.updateState({
	sections: ["app", "component", "data"],
	section: "app",
	categories: ["All"],
	selected: "All",
	busy: null,
	entries: [
		{id: "gimp", kind: "app", name: "GIMP", summary: "Images",
			category: "Graphics", installed: true, requires: "", icon: ""}
	]
});
ids.entries.children[0].click();
if (ids["detail-action"].textContent !== "Remove")
	throw new Error("expected Remove for installed entry");

// A component entry can require another one.
window.Store.updateState({
	sections: ["app", "component", "data"],
	section: "component",
	categories: ["All"],
	selected: "All",
	busy: null,
	entries: [
		{id: "aros-workbench", kind: "component", name: "AROS Workbench",
			summary: "Desktop", category: "Components", installed: false,
			requires: "aros", icon: ""}
	]
});
ids.entries.children[0].click();
if (ids["detail-requires"].className === "hidden")
	throw new Error("requires line should show for a dependent component");
if (ids["detail-requires"].textContent !== "Requires component: aros")
	throw new Error("requires text wrong: "
		+ ids["detail-requires"].textContent);

// No remote resources in the template.
if (/https?:\/\//.test(html) || /https?:\/\//.test(js) || /https?:\/\//.test(css))
	throw new Error("template references a remote URL");

console.log("page_test: all assertions passed");

// An identical push does not rebuild the entry nodes.
{
	window.Store.updateState(JSON.parse(JSON.stringify(state)));
	const first = ids.entries.children[0];
	window.Store.updateState(JSON.parse(JSON.stringify(state)));
	if (ids.entries.children[0] !== first)
		throw new Error("unchanged state must not rebuild entries");
	window.Store.updateState(Object.assign({}, state, {entries: [state.entries[1]]}));
	if (ids.entries.children.length !== 1)
		throw new Error("changed entries must rebuild");
}
