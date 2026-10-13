/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * WebMail native UI: talks to the C++ session through the webmail
 * script message handler; C++ pushes results back with
 * vosMail.dispatchEvent().
 */
"use strict";

var vosMail = (function () {
	var state = {
		folder: "INBOX",
		folders: [],
		headers: [],
		offset: 1,
		pageSize: 50,
		selected: null,
		attachments: [],
		composeOpen: false
	};

	function post(payload) {
		try {
			window.webkit.messageHandlers.webmail.postMessage(
				JSON.stringify(payload));
		} catch (e) {
			console.log("post failed", e);
		}
	}

	function esc(text) {
		var div = document.createElement("div");
		div.textContent = text == null ? "" : String(text);
		return div.innerHTML;
	}

	function $(id) { return document.getElementById(id); }

	function renderFolders() {
		var host = $("folders");
		host.innerHTML = "";
		state.folders.forEach(function (folder) {
			var el = document.createElement("div");
			el.className = "folder" + (folder.name === state.folder
				? " selected" : "");
			el.textContent = folder.display || folder.name;
			el.addEventListener("click", function () {
				state.folder = folder.name;
				state.offset = 1;
				renderFolders();
				requestHeaders();
			});
			host.appendChild(el);
		});
	}

	function renderHeaders() {
		var host = $("headers");
		host.innerHTML = "";
		state.headers.forEach(function (message) {
			var el = document.createElement("div");
			el.className = "header" + (message.uid === state.selected
				? " selected" : "") + (message.seen ? "" : " unseen");
			el.innerHTML = "<span class='from'>" + esc(message.fromName ||
				message.fromAddr) + "</span><span class='subject'>" +
				esc(message.subject) + "</span>";
			el.addEventListener("click", function () {
				state.selected = message.uid;
				renderHeaders();
				post({ cmd: "message", folder: state.folder,
					uid: message.uid });
			});
			host.appendChild(el);
		});
	}

	function renderMessage(message) {
		$("reader-empty").hidden = true;
		$("reader-content").hidden = false;
		$("reader-subject").textContent = message.subject || "";
		$("reader-from").textContent = (message.fromName || "") + " " +
			(message.fromAddr || "");
		$("reader-text").textContent = message.text || "";
		var frameWrap = $("reader-frame-wrap");
		if (message.html && message.html.length > 0) {
			frameWrap.hidden = false;
			$("reader-text").hidden = true;
			var frame = $("reader-frame");
			frame.srcdoc = message.html;
		} else {
			frameWrap.hidden = true;
			$("reader-text").hidden = false;
			$("reader-frame").srcdoc = "";
		}
		var attachments = $("reader-attachments");
		attachments.innerHTML = "";
		(message.attachments || []).forEach(function (name) {
			var el = document.createElement("span");
			el.className = "attachment";
			el.textContent = name;
			attachments.appendChild(el);
		});
	}

	function requestFolders() { post({ cmd: "folders" }); }
	function requestHeaders() {
		post({ cmd: "headers", folder: state.folder,
			offset: state.offset, count: state.pageSize });
	}

	function openCompose(to, subject, body) {
		state.composeOpen = true;
		$("compose").hidden = false;
		$("c-to").value = to || "";
		$("c-subject").value = subject || "";
		$("c-body").value = body || "";
		$("c-cc").value = "";
		$("c-bcc").value = "";
		state.attachments = [];
		$("c-attachments").innerHTML = "";
	}

	function closeCompose() {
		state.composeOpen = false;
		$("compose").hidden = true;
	}

	function collectCompose() {
		function list(value) {
			return value.split(",").map(function (entry) {
				entry = entry.trim();
				if (!entry) return null;
				var at = entry.indexOf("<");
				if (at >= 0) {
					return { name: entry.substring(0, at).trim()
						.replace(/"/g, ""),
						address: entry.substring(at + 1)
							.replace(/>/g, "").trim() };
				}
				return { name: "", address: entry };
			}).filter(Boolean);
		}
		return {
			to: list($("c-to").value),
			cc: list($("c-cc").value),
			bcc: list($("c-bcc").value),
			subject: $("c-subject").value,
			body: $("c-body").value,
			attachments: state.attachments.slice()
		};
	}

	function dispatchEvent(event) {
		if (!event || !event.type) return;
		switch (event.type) {
			case "folders":
				state.folders = event.folders || [];
				renderFolders();
				requestHeaders();
				break;
			case "headers":
				state.headers = event.messages || [];
				renderHeaders();
				break;
			case "message":
				renderMessage(event);
				break;
			case "newMail":
				requestHeaders();
				break;
			case "sent":
				$("status").textContent = "Message sent";
				closeCompose();
				break;
			case "error":
				$("status").textContent = event.message || "Error";
				break;
			default:
				break;
		}
	}

	function wire() {
		$("btn-check").addEventListener("click", requestHeaders);
		$("btn-compose").addEventListener("click", function () {
			openCompose("", "", "");
		});
		$("btn-compose-close").addEventListener("click", closeCompose);
		$("btn-send").addEventListener("click", function () {
			post({ cmd: "send", compose: collectCompose() });
		});
		$("btn-draft").addEventListener("click", function () {
			post({ cmd: "draft", compose: collectCompose() });
		});
		$("btn-attach").addEventListener("click", function () {
			post({ cmd: "pickAttachment" });
		});
		$("btn-reply").addEventListener("click", function () {
			var message = currentMessage();
			openCompose(message ? message.fromAddr : "", "Re: " +
				(message ? message.subject : ""), "");
		});
		$("btn-forward").addEventListener("click", function () {
			var message = currentMessage();
			openCompose("", "Fwd: " + (message ? message.subject : ""),
				message ? message.text : "");
		});
		$("btn-delete").addEventListener("click", function () {
			if (state.selected != null) {
				post({ cmd: "delete", folder: state.folder,
					uids: [state.selected] });
				state.selected = null;
				requestHeaders();
			}
		});
		$("btn-prev").addEventListener("click", function () {
			state.offset = Math.max(1, state.offset - state.pageSize);
			requestHeaders();
		});
		$("btn-next").addEventListener("click", function () {
			state.offset = state.offset + state.pageSize;
			requestHeaders();
		});
		$("search").addEventListener("keydown", function (event) {
			if (event.key === "Enter") {
				post({ cmd: "search", folder: state.folder,
					criteria: "SUBJECT " + $("search").value });
			}
		});
	}

	function currentMessage() {
		for (var i = 0; i < state.headers.length; i++) {
			if (state.headers[i].uid === state.selected)
				return state.headers[i];
		}
		return null;
	}

	function attach(path) {
		if (!path) return;
		state.attachments.push(path);
		var el = document.createElement("span");
		el.className = "attachment";
		el.textContent = path.split("/").pop();
		$("c-attachments").appendChild(el);
	}

	document.addEventListener("DOMContentLoaded", function () {
		wire();
		requestFolders();
	});

	return {
		dispatchEvent: dispatchEvent,
		openCompose: openCompose,
		attach: attach
	};
})();
