/* Store: page logic. Talks to C++ through
 * window.webkit.messageHandlers.store ("<action>|<argument>"). */

(function () {
	"use strict";

	var sectionNames = { app: "Apps", component: "Components",
		data: "Data", installed: "Installed" };
	var state = { sections: ["app", "component", "data", "installed"],
		section: "app",
		categories: [], selected: "All", entries: [], total: 0,
		detail: null, busy: null };
	var selectedEntryId = null;
	var searchTimer = null;
	// Cheap keys of what each region last rendered.
	var keys = { sections: null, categories: null, entries: null,
		detail: null };
	// Per rendered entry: id and a key of what its node shows.
	var rendered = [];
	var moreRequested = -1;
	function letterNode(name) {
		return el("div", "letter", name.charAt(0).toUpperCase());
	}

	function post(action, argument) {
		// Any navigation drops the last result line.
		if (action !== "install" && action !== "remove"
				&& action !== "launch" && action !== "ready" && action !== "visible") {
			document.getElementById("status").textContent = "";
		}
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
		var key = state.sections.join(",") + "|" + state.section;
		if (key === keys.sections)
			return;
		keys.sections = key;
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
		var key = state.section + "|" + state.selected + "|"
			+ state.categories.join(",");
		if (key === keys.categories)
			return;
		keys.categories = key;
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

	function entryKey(entry) {
		return JSON.stringify(entry);
	}

	function buildEntry(entry) {
		var item = el("li", "");
		item.setAttribute("data-id", entry.id);

		if (entry.icon) {
			// The letter tile stays until the icon has loaded.
			var tile = letterNode(entry.name);
			var img = el("img", "icon pending");
			img.alt = "";
			img.loading = "lazy";
			img.decoding = "async";
			img.onload = function () {
				img.className = "icon";
				if (tile.parentNode)
					tile.parentNode.removeChild(tile);
			};
			img.src = entry.icon;
			item.appendChild(tile);
			item.appendChild(img);
		} else
			item.appendChild(letterNode(entry.name));

		var text = el("div", "text");
		text.appendChild(el("div", "name", entry.name));
		text.appendChild(el("div", "summary", entry.summary));
		item.appendChild(text);

		if (entry.installed)
			item.appendChild(el("span", "badge", "Installed"));

		item.addEventListener("click", function () {
			selectedEntryId = entry.id;
			post("info", entry.id);
			renderDetail();
			document.getElementById("content").scrollTop = 0;
		});
		return item;
	}

	function requestMore() {
		var count = state.entries.length;
		if (count >= state.total || moreRequested === count)
			return;
		moreRequested = count;
		post("more");
	}

	function renderMore() {
		var more = document.getElementById("more");
		if (!more)
			return;
		more.className = state.entries.length < state.total ? "" : "hidden";
		more.textContent = "Show more (" + state.entries.length + " of "
			+ state.total + ")";
	}

	// Only changed entries are rebuilt; a longer list with the same head
	// just appends.
	function renderEntries() {
		var list = document.getElementById("entries");
		var shown = state.entries;
		var key = state.section + "|" + state.total + "|"
			+ JSON.stringify(shown);
		if (key === keys.entries)
			return;
		keys.entries = key;

		var reuse = shown.length >= rendered.length;
		for (var i = 0; reuse && i < rendered.length; i++) {
			if (rendered[i].id !== shown[i].id)
				reuse = false;
		}
		if (!reuse) {
			list.textContent = "";
			rendered = [];
		}
		for (var n = 0; n < shown.length; n++) {
			var entryKeyValue = entryKey(shown[n]);
			if (n < rendered.length) {
				if (rendered[n].key !== entryKeyValue) {
					list.replaceChild(buildEntry(shown[n]),
						list.children[n]);
					rendered[n].key = entryKeyValue;
				}
			} else {
				list.appendChild(buildEntry(shown[n]));
				rendered.push({ id: shown[n].id, key: entryKeyValue });
			}
		}
		renderMore();
	}

	function currentEntry() {
		if (state.detail && state.detail.id === selectedEntryId)
			return state.detail;
		for (var i = 0; i < state.entries.length; i++) {
			if (state.entries[i].id === selectedEntryId)
				return state.entries[i];
		}
		return null;
	}

	function renderDetail() {
		var detail = document.getElementById("detail");
		var entry = currentEntry();
		var key = entry ? JSON.stringify(entry) + "|" + (state.busy !== null)
			+ "|" + (state.busy ? state.busy.cancel : "")
			: null;
		if (key !== null && key === keys.detail) {
			renderProgress();
			return;
		}
		keys.detail = key;
		var list = document.getElementById("entries");
		if (!entry) {
			detail.className = "hidden";
			list.className = "";
			return;
		}
		detail.className = "";
		list.className = "hidden";

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
			icon.onload = function () {
				icon.className = "icon";
				letter.className = "letter hidden";
			};
			icon.className = "icon hidden";
			letter.className = "letter";
			letter.textContent = entry.name.charAt(0).toUpperCase();
			icon.src = entry.icon;
		} else {
			icon.className = "icon hidden";
			letter.className = "letter";
			letter.textContent = entry.name.charAt(0).toUpperCase();
		}

		document.getElementById("detail-name").textContent = entry.name;
		document.getElementById("detail-summary").textContent = entry.summary;

		document.getElementById("detail-meta").textContent
			= (entry.category ? "Category: " + entry.category : "")
			+ (entry.packages && entry.packages.length
				? " | Packages: " + entry.packages.join(", ") : "")
			+ " | " + (entry.installed ? "Installed" : "Not installed");

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

		var launch = document.getElementById("detail-launch");
		if (entry.launch && !busy) {
			launch.className = "";
			launch.onclick = function () { post("launch", entry.id); };
		} else
			launch.className = "hidden";

		renderProgress();
	}

	function renderProgress() {
		var progress = document.getElementById("progress");
		var bar = document.getElementById("progress-bar");
		var text = document.getElementById("progress-text");
		var cancel = document.getElementById("detail-cancel");
		if (!state.busy || state.busy.id !== selectedEntryId) {
			progress.className = "hidden";
			cancel.className = "hidden";
			return;
		}
		cancel.className = "";
		cancel.disabled = state.busy.cancel !== 1;
		cancel.textContent = "Cancel";
		cancel.title = state.busy.cancel === 2
			? "Cancelling after the current step"
			: state.busy.cancel === 0
				? "Not available until the changes are confirmed"
				: "Stops after the current step; a running step finishes";
		cancel.onclick = function () { post("cancel", ""); };
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
			if (state.entries.length > moreRequested)
				moreRequested = -1;
			if (state.info)
				selectedEntryId = state.info;
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
		var search = document.getElementById("search");
		search.addEventListener("input", function () {
			var value = this.value.trim().toLowerCase();
			if (searchTimer !== null)
				clearTimeout(searchTimer);
			searchTimer = setTimeout(function () {
				searchTimer = null;
				selectedEntryId = null;
				renderDetail();
				post("search", value);
			}, 200);
		});
		var more = document.getElementById("more");
		if (more)
			more.addEventListener("click", requestMore);
		var content = document.getElementById("content");
		content.addEventListener("scroll", function () {
			if (this.scrollTop + this.clientHeight
					>= this.scrollHeight - 200)
				requestMore();
		});
		document.getElementById("detail-back").addEventListener("click",
			function () {
				selectedEntryId = null;
				post("info", "");
				renderDetail();
			});
		renderAll();
		post("ready");
	});
})();
