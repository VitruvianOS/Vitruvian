/* Store: page logic. Talks to C++ through
 * window.webkit.messageHandlers.store ("<action>|<argument>"). */

(function () {
	"use strict";

	var sectionNames = { app: "Apps", component: "Components",
		data: "Data" };
	var state = { sections: ["app", "component", "data"], section: "app",
		categories: [], selected: "All", entries: [], busy: null };
	var selectedEntryId = null;

	function post(action, argument) {
		if (window.webkit && window.webkit.messageHandlers
				&& window.webkit.messageHandlers.store) {
			window.webkit.messageHandlers.store.postMessage(
				argument ? action + "|" + argument : action);
		}
	}

	function el(tag, className, text) {
		var node = document.createElement(tag);
		if (className)
			node.className = className;
		if (text !== undefined)
			node.textContent = text;
		return node;
	}

	function renderSections() {
		var list = document.getElementById("sections");
		list.textContent = "";
		state.sections.forEach(function (kind) {
			var item = el("li", kind === state.section
				? "selected" : "", sectionNames[kind] || kind);
			item.addEventListener("click", function () {
				if (kind === state.section)
					return;
				selectedEntryId = null;
				post("section", kind);
			});
			list.appendChild(item);
		});
	}

	function renderCategories() {
		var list = document.getElementById("categories");
		list.textContent = "";
		// Only the apps section filters by category.
		if (state.section !== "app" || state.categories.length < 2) {
			list.className = "hidden";
			return;
		}
		list.className = "";
		state.categories.forEach(function (category) {
			var item = el("li", category === state.selected
				? "selected" : "", category);
			item.addEventListener("click", function () {
				selectedEntryId = null;
				post("select", category);
			});
			list.appendChild(item);
		});
	}

	function renderEntries() {
		var list = document.getElementById("entries");
		list.textContent = "";
		state.entries.forEach(function (entry) {
			var item = el("li", entry.id === selectedEntryId
				? "selected" : "");

			if (entry.icon) {
				var img = el("img", "icon");
				img.src = entry.icon;
				img.alt = "";
				item.appendChild(img);
			} else {
				item.appendChild(el("div", "letter",
					entry.name.charAt(0).toUpperCase()));
			}

			var text = el("div", "text");
			text.appendChild(el("div", "name", entry.name));
			text.appendChild(el("div", "summary", entry.summary));
			item.appendChild(text);

			if (entry.installed)
				item.appendChild(el("span", "badge", "Installed"));

			item.addEventListener("click", function () {
				selectedEntryId = entry.id;
				renderEntries();
				renderDetail();
			});
			list.appendChild(item);
		});
	}

	function currentEntry() {
		for (var i = 0; i < state.entries.length; i++) {
			if (state.entries[i].id === selectedEntryId)
				return state.entries[i];
		}
		return null;
	}

	function renderDetail() {
		var detail = document.getElementById("detail");
		var entry = currentEntry();
		if (!entry) {
			detail.className = "hidden";
			return;
		}
		detail.className = "";

		// Artwork entries lead with their preview strip.
		var previews = document.getElementById("detail-previews");
		previews.textContent = "";
		if (entry.previews && entry.previews.length > 0) {
			entry.previews.forEach(function (src) {
				var img = el("img", "preview");
				img.src = src;
				img.alt = "";
				previews.appendChild(img);
			});
			previews.className = "previews";
		} else
			previews.className = "previews hidden";

		var icon = document.getElementById("detail-icon");
		var letter = document.getElementById("detail-letter");
		if (entry.icon) {
			icon.src = entry.icon;
			icon.className = "icon";
			letter.className = "letter hidden";
		} else {
			icon.className = "icon hidden";
			letter.className = "letter";
			letter.textContent = entry.name.charAt(0).toUpperCase();
		}

		document.getElementById("detail-name").textContent = entry.name;
		document.getElementById("detail-summary").textContent = entry.summary;

		var requires = document.getElementById("detail-requires");
		if (entry.requires) {
			requires.textContent = "Requires component: " + entry.requires;
			requires.className = "requires";
		} else
			requires.className = "requires hidden";

		var action = document.getElementById("detail-action");
		var busy = state.busy !== null;
		action.disabled = busy;
		if (entry.installed) {
			action.textContent = "Remove";
			action.className = "remove";
			action.onclick = function () { post("remove", entry.id); };
		} else {
			action.textContent = "Install";
			action.className = "";
			action.onclick = function () { post("install", entry.id); };
		}

		renderProgress();
	}

	function renderProgress() {
		var progress = document.getElementById("progress");
		var bar = document.getElementById("progress-bar");
		var text = document.getElementById("progress-text");
		if (!state.busy || state.busy.id !== selectedEntryId) {
			progress.className = "hidden";
			return;
		}
		progress.className = "";
		var percent = state.busy.percent >= 0 ? state.busy.percent : 0;
		bar.style.width = percent + "%";
		text.textContent = (state.busy.status || "")
			+ (state.busy.percent >= 0 ? " " + percent + "%" : "");
	}

	function renderAll() {
		renderSections();
		renderCategories();
		renderEntries();
		renderDetail();
	}

	window.Store = {
		updateState: function (newState) {
			state = newState;
			renderAll();
		},
		setStatus: function (text) {
			document.getElementById("status").textContent = text || "";
		},
		onProgress: function (percent, text) {
			if (state.busy) {
				state.busy.percent = percent;
				state.busy.status = text;
			}
			renderProgress();
		}
	};

	document.addEventListener("DOMContentLoaded", function () {
		renderAll();
		post("ready");
	});
})();
