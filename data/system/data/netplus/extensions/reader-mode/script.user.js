/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
// ==UserScript==
// @name        NetPlus Reader Mode
// @version     1.0
// @description Readability-style article view of the current page
// @match       <all_urls>
// @run-at      document-end
// @grant       none
// ==End==

(function () {
	'use strict';

	var overlay = null;
	var savedOverflow = '';

	function scoreParagraphs(root) {
		var best = null;
		var bestScore = 0;
		var nodes = root.querySelectorAll('div, section, main, td');
		for (var i = 0; i < nodes.length; i++) {
			var node = nodes[i];
			var paragraphs = node.querySelectorAll(':scope > p');
			if (paragraphs.length < 3)
				continue;
			var length = 0;
			for (var j = 0; j < paragraphs.length; j++)
				length += paragraphs[j].textContent.trim().length;
			if (length > bestScore) {
				bestScore = length;
				best = node;
			}
		}
		return bestScore > 200 ? best : null;
	}

	function findArticle() {
		var article = document.querySelector('article');
		if (article && article.textContent.trim().length > 200)
			return article;
		var main = document.querySelector('main');
		if (main) {
			var inMain = scoreParagraphs(main);
			if (inMain)
				return inMain;
		}
		return scoreParagraphs(document.body);
	}

	function collectText(article) {
		var paragraphs = article.querySelectorAll('p, li, blockquote, pre');
		var out = [];
		for (var i = 0; i < paragraphs.length; i++) {
			var text = paragraphs[i].textContent.trim();
			if (text.length > 40)
				out.push(text);
		}
		return out;
	}

	function close() {
		if (overlay == null)
			return;
		overlay.parentNode.removeChild(overlay);
		overlay = null;
		document.removeEventListener('keydown', onKey, true);
		document.documentElement.style.overflow = savedOverflow;
	}

	function onKey(event) {
		if (event.key === 'Escape')
			close();
	}

	function open() {
		var article = findArticle();
		if (article == null)
			return false;
		var title = document.title || location.host;
		var paragraphs = collectText(article);
		if (paragraphs.length === 0)
			return false;

		savedOverflow = document.documentElement.style.overflow;
		document.documentElement.style.overflow = 'hidden';

		overlay = document.createElement('div');
		overlay.style.cssText =
			'position:fixed;inset:0;z-index:2147483647;overflow-y:auto;' +
			'background:#faf6ee;color:#222;font:18px/1.7 Georgia,serif;' +
			'padding:0 0 20vh 0;margin:0;';

		var box = document.createElement('div');
		box.style.cssText =
			'max-width:42em;margin:0 auto;padding:3em 1.5em;';

		var closeBtn = document.createElement('button');
		closeBtn.textContent = '\u00d7';
		closeBtn.title = 'Close reader view (Escape)';
		closeBtn.style.cssText =
			'position:fixed;top:1em;right:1em;width:2.4em;height:2.4em;' +
			'border:none;border-radius:50%;background:#222;color:#faf6ee;' +
			'font-size:1.4em;cursor:pointer;';
		closeBtn.addEventListener('click', close);

		var heading = document.createElement('h1');
		heading.textContent = title;
		heading.style.cssText =
			'font-size:1.9em;line-height:1.25;margin:0 0 1.2em;';

		var source = document.createElement('p');
		source.textContent = location.href;
		source.style.cssText =
			'font-size:0.8em;color:#666;margin:0 0 2.5em;word-break:break-all;';

		box.appendChild(heading);
		box.appendChild(source);
		for (var i = 0; i < paragraphs.length; i++) {
			var p = document.createElement('p');
			p.textContent = paragraphs[i];
			box.appendChild(p);
		}
		overlay.appendChild(closeBtn);
		overlay.appendChild(box);
		overlay.addEventListener('click', function (event) {
			if (event.target === overlay)
				close();
		});
		document.body.appendChild(overlay);
		document.addEventListener('keydown', onKey, true);
		return true;
	}

	window.__netplusReader = function () {
		if (overlay != null) {
			close();
			return true;
		}
		return open();
	};
})();
