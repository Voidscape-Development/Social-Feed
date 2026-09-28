/* Social Feed overlay runtime shared by chat.html and events.html.
 *
 * The plugin loads these pages from its loopback server as http://127.0.0.1:<port>/overlay/<page>?source=<token>.
 * The initial config comes from GET /api/source/<token>/config; afterwards the plugin pushes
 * {type, payload} envelopes through obs-browser as a `socialFeed` CustomEvent on window:
 *   config      full config (design, channels, accounts, demo)
 *   chat        normalized chat message
 *   event       normalized event (follow, subscription, ...)
 *   moderation  {action: delete_message | clear_user | clear_chat, target}
 */
(function () {
	'use strict';

	const SF = (window.SF = {});
	const handlers = { config: [], chat: [], event: [], moderation: [] };

	SF.token = new URLSearchParams(location.search).get('source') || '';
	SF.config = null;

	SF.on = function (type, fn) {
		(handlers[type] = handlers[type] || []).push(fn);
	};

	SF.emit = function (type, payload) {
		for (const fn of handlers[type] || []) {
			try {
				fn(payload);
			} catch (err) {
				console.error('[SocialFeed]', type, err);
			}
		}
	};

	SF.setConfig = function (config) {
		if (!config || !config.design)
			return;
		SF.config = config;
		SF.applyCustomCss(config.design.customCss || '');
		SF.emit('config', config);
	};

	window.addEventListener('socialFeed', function (e) {
		const envelope = e.detail;
		if (!envelope || !envelope.type)
			return;
		if (envelope.type === 'config')
			SF.setConfig(envelope.payload);
		else if (SF.config)
			SF.emit(envelope.type, envelope.payload);
	});

	SF.loadConfig = async function () {
		for (let attempt = 0; attempt < 30 && !SF.config; attempt++) {
			try {
				const response = await fetch('/api/source/' + encodeURIComponent(SF.token) + '/config', { cache: 'no-store' });
				if (response.ok) {
					SF.setConfig(await response.json());
					return;
				}
			} catch (err) {
				/* server not reachable yet */
			}
			await SF.sleep(Math.min(5000, 250 * (attempt + 1)));
		}
	};

	/* ---- helpers ---- */

	SF.sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

	SF.escape = function (text) {
		return String(text == null ? '' : text)
			.replace(/&/g, '&amp;')
			.replace(/</g, '&lt;')
			.replace(/>/g, '&gt;')
			.replace(/"/g, '&quot;')
			.replace(/'/g, '&#39;');
	};

	/* #RRGGBBAA -> rgba() (Chromium understands 8-digit hex too, this keeps older CEF safe). */
	SF.color = function (value, fallback) {
		const v = String(value || '').trim();
		const m = /^#([0-9a-f]{6})([0-9a-f]{2})?$/i.exec(v);
		if (!m)
			return v || fallback || 'transparent';
		const n = parseInt(m[1], 16);
		const a = m[2] ? parseInt(m[2], 16) / 255 : 1;
		return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${a.toFixed(3)})`;
	};

	SF.platformColors = {
		twitch: '#9146FF',
		youtube: '#FF0033',
		kick: '#53FC18',
		tiktok: '#FE2C55',
		streamelements: '#3B6CFF',
		streamlabs: '#31C3A2',
		streamerbot: '#7E3FF2',
	};

	SF.platformLabels = {
		twitch: 'Twitch',
		youtube: 'YouTube',
		kick: 'Kick',
		tiktok: 'TikTok',
		streamelements: 'StreamElements',
		streamlabs: 'Streamlabs',
		streamerbot: 'Streamer.bot',
	};

	SF.platformIcon = function (platform) {
		const el = document.createElement('span');
		el.className = 'sf-platform-icon';
		el.title = SF.platformLabels[platform] || platform;
		el.textContent = (SF.platformLabels[platform] || platform || '?').charAt(0);
		el.style.background = SF.platformColors[platform] || '#666';
		return el;
	};

	SF.setVars = function (element, vars) {
		for (const [key, value] of Object.entries(vars))
			element.style.setProperty(key, value);
	};

	let customStyle = null;
	SF.applyCustomCss = function (css) {
		if (!customStyle) {
			customStyle = document.createElement('style');
			customStyle.id = 'sf-custom-css';
			document.head.appendChild(customStyle);
		}
		customStyle.textContent = css;
	};

	/* Runs a keyframe animation (sf-in-NAME or sf-out-NAME) and resolves when it ends. */
	SF.animate = function (element, direction, name, duration, easing) {
		return new Promise((resolve) => {
			const ms = Math.max(0, Number(duration) || 0);
			if (!name || ms === 0) {
				element.style.animation = '';
				if (direction === 'out')
					element.style.opacity = '0';
				resolve();
				return;
			}
			let done = false;
			const finish = () => {
				if (done)
					return;
				done = true;
				element.removeEventListener('animationend', onEnd);
				resolve();
			};
			const onEnd = (e) => {
				if (e.target === element)
					finish();
			};
			element.addEventListener('animationend', onEnd);
			element.style.animation = 'none';
			void element.offsetWidth; /* restart */
			element.style.animation = `sf-${direction}-${name} ${ms}ms ${easing || 'ease'} both`;
			setTimeout(finish, ms + 100);
		});
	};

	/* FLIP: call before a layout change, then call the returned function after it to smoothly
	 * move elements from their old to their new position. */
	SF.flip = function (elements, duration) {
		const first = new Map();
		for (const el of elements)
			first.set(el, el.getBoundingClientRect());
		return function play() {
			for (const [el, rect] of first) {
				if (!el.isConnected)
					continue;
				const last = el.getBoundingClientRect();
				const dx = rect.left - last.left;
				const dy = rect.top - last.top;
				if (!dx && !dy)
					continue;
				el.animate([{ transform: `translate(${dx}px, ${dy}px)` }, { transform: 'none' }], {
					duration: duration || 250,
					easing: 'ease-out',
					composite: 'add',
				});
			}
		};
	};

	SF.tierLabel = function (tier) {
		if (!tier)
			return '';
		const t = String(tier).toLowerCase();
		if (t === 'prime')
			return 'Prime';
		if (t === '1000')
			return 'Tier 1';
		if (t === '2000')
			return 'Tier 2';
		if (t === '3000')
			return 'Tier 3';
		return String(tier);
	};

	SF.isVideo = function (url) {
		return /\.(webm|mp4|mov|m4v)(\?|$)/i.test(url || '');
	};

	SF.start = function () {
		SF.loadConfig();
	};
})();
