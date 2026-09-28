/* Social Feed: Chat Feed renderer. */
(function () {
	'use strict';

	const SF = window.SF;
	const feed = document.getElementById('feed');
	let design = null;
	let queue = Promise.resolve();

	const ROLE_RANK = { everyone: 0, subscriber: 1, vip: 2, moderator: 3, broadcaster: 4 };
	const HIGHLIGHT_ORDER = ['mention', 'firstTime', 'cheer', 'broadcaster', 'moderator', 'vip', 'subscriber'];
	const FALLBACK_NAME_COLORS = ['#FF4A80', '#FF7A45', '#FFC53D', '#73D13D', '#36CFC9', '#40A9FF', '#9254DE', '#F759AB'];
	const LINK_RE = /(\bhttps?:\/\/\S+|\bwww\.\S+|\b[\w-]+\.(?:com|net|org|tv|gg|io|ly|me|co|app|dev|xyz)\b(?:\/\S*)?)/i;

	/* ---- design ---- */

	function applyDesign(d) {
		design = d;
		const body = document.body;
		const layout = d.layout, text = d.text, bubble = d.bubble;

		body.className = [
			'sf-newest-' + (layout.direction === 'newest-top' ? 'top' : 'bottom'),
			'sf-align-' + layout.align,
			layout.messageLayout === 'stacked' ? 'sf-stacked' : 'sf-inline',
			bubble.enabled ? 'sf-bubbles' : '',
			bubble.accent !== 'none' && bubble.accentWidth > 0 ? 'sf-accent' : '',
		].join(' ');

		const shadow = text.shadow ? `0 1px ${text.shadowBlur}px ${SF.color(text.shadowColor)}` : 'none';
		SF.setVars(document.documentElement, {
			'--sf-font': text.fontFamily,
			'--sf-size': text.fontSize + 'px',
			'--sf-weight': text.fontWeight,
			'--sf-line-height': text.lineHeight,
			'--sf-color': SF.color(text.color),
			'--sf-name-weight': text.nameFontWeight,
			'--sf-emote-size': text.emoteScale + 'em',
			'--sf-text-shadow': shadow,
			'--sf-outline-width': (text.outlineWidth || 0) + 'px',
			'--sf-outline-color': SF.color(text.outlineColor),
			'--sf-padding': layout.padding + 'px',
			'--sf-gap': layout.gap + 'px',
			'--sf-bubble-bg': SF.color(bubble.background),
			'--sf-bubble-radius': bubble.radius + 'px',
			'--sf-bubble-padding': bubble.padding + 'px',
			'--sf-bubble-border-width': bubble.borderWidth + 'px',
			'--sf-bubble-border-color': SF.color(bubble.borderColor),
			'--sf-accent-width': bubble.accentWidth + 'px',
			'--sf-bubble-max-width': bubble.maxWidth + '%',
		});
		enforceLimits();
	}

	/* ---- filtering ---- */

	function userRank(user) {
		let rank = 0;
		for (const role of user.roles || [])
			rank = Math.max(rank, ROLE_RANK[role] || 0);
		return rank;
	}

	function lowerList(list) {
		return (list || []).map((v) => String(v).toLowerCase().trim()).filter(Boolean);
	}

	function escapeRe(text) {
		return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
	}

	function passesFilters(msg) {
		const f = design.filters;
		const user = msg.user || {};
		const login = String(user.login || user.displayName || '').toLowerCase();
		const text = String(msg.text || '');
		const lower = text.toLowerCase();

		if (f.platforms && f.platforms[msg.platform] === false)
			return false;
		if (userRank(user) < (ROLE_RANK[f.minRole] || 0))
			return false;
		if (f.hideBots && lowerList(f.bots).includes(login))
			return false;
		if (lowerList(f.blockedUsers).includes(login))
			return false;
		if (f.hideCommands) {
			const prefixes = String(f.commandPrefixes || '!').replace(/\s+/g, '').split('');
			if (prefixes.some((p) => text.trimStart().startsWith(p)))
				return false;
		}
		if (f.blockedWordAction === 'hide' && lowerList(f.blockedWords).some((w) => lower.includes(w)))
			return false;
		if (f.links === 'hide-message' && LINK_RE.test(text))
			return false;
		if (f.minLength > 0 && text.trim().length < f.minLength)
			return false;
		return true;
	}

	/* ---- building ---- */

	function nameColor(msg) {
		const t = design.text;
		if (t.nameColorMode === 'custom')
			return SF.color(t.nameColor);
		if (t.nameColorMode === 'platform')
			return SF.platformColors[msg.platform] || '#fff';
		if (msg.user && msg.user.color)
			return msg.user.color;
		const name = (msg.user && msg.user.displayName) || '';
		let hash = 0;
		for (const ch of name)
			hash = (hash * 31 + ch.charCodeAt(0)) | 0;
		return FALLBACK_NAME_COLORS[Math.abs(hash) % FALLBACK_NAME_COLORS.length];
	}

	function pickHighlight(msg) {
		const h = design.highlights;
		const roles = (msg.user && msg.user.roles) || [];
		const text = String(msg.text || '').toLowerCase();
		const own = Object.values((SF.config && SF.config.accounts) || {}).map((a) => String(a).toLowerCase());
		const keywords = lowerList(h.mention && h.mention.keywords);
		const mentioned = own.some((login) => login && text.includes('@' + login)) ||
			keywords.some((k) => text.includes(k));

		const active = {
			mention: mentioned,
			firstTime: !!msg.firstMessage,
			cheer: (msg.bits || 0) > 0 || !!msg.highlighted || !!msg.paid,
			broadcaster: roles.includes('broadcaster'),
			moderator: roles.includes('moderator'),
			vip: roles.includes('vip'),
			subscriber: roles.includes('subscriber'),
		};
		for (const key of HIGHLIGHT_ORDER) {
			if (active[key] && h[key] && h[key].enabled)
				return { key, color: h[key].color };
		}
		return null;
	}

	function emoteImg(url, name, extraClass) {
		const img = document.createElement('img');
		img.className = 'sf-emote' + (extraClass ? ' ' + extraClass : '');
		img.src = url;
		img.alt = name;
		img.title = name;
		return img;
	}

	function appendText(container, text, emotes) {
		const f = design.filters;
		let value = text;
		if (f.links === 'redact')
			value = value.replace(new RegExp(LINK_RE.source, 'gi'), '<link>');
		if (f.blockedWordAction === 'censor') {
			for (const word of lowerList(f.blockedWords))
				value = value.replace(new RegExp(escapeRe(word), 'gi'), (m) => '*'.repeat(m.length));
		}

		if (!emotes) {
			container.appendChild(document.createTextNode(value));
			return 0;
		}

		let emoteCount = 0;
		let lastEmote = null;
		for (const part of value.split(/(\s+)/)) {
			if (!part)
				continue;
			const emote = /\s/.test(part) ? null : emotes.get(part);
			if (!emote) {
				container.appendChild(document.createTextNode(part));
				if (!/^\s+$/.test(part))
					lastEmote = null;
				continue;
			}
			emoteCount++;
			if (emote.zeroWidth && lastEmote) {
				/* Stack zero-width 7TV emotes on top of the previous emote. */
				let stack = lastEmote.parentElement;
				if (!stack.classList.contains('sf-emote-stack')) {
					stack = document.createElement('span');
					stack.className = 'sf-emote-stack';
					lastEmote.replaceWith(stack);
					stack.appendChild(lastEmote);
				}
				stack.appendChild(emoteImg(emote.url, part, 'sf-emote-zw'));
				continue;
			}
			/* Trim the whitespace between two stacked emotes. */
			lastEmote = emoteImg(emote.url, part);
			container.appendChild(lastEmote);
		}
		return emoteCount;
	}

	function buildText(msg, emotes) {
		const el = document.createElement('span');
		el.className = 'sf-text';
		let emoteCount = 0;
		const fragments = msg.fragments && msg.fragments.length ? msg.fragments : [{ type: 'text', text: msg.text || '' }];

		for (const fragment of fragments) {
			if (fragment.type === 'emote' && fragment.url) {
				let url = fragment.url;
				if (!design.emotes.animated)
					url = url.replace('/default/', '/static/');
				el.appendChild(emoteImg(url, fragment.text));
				emoteCount++;
			} else {
				emoteCount += appendText(el, fragment.text || '', emotes);
			}
		}
		/* Characters that were turned into third-party emotes do not count as text. */
		const remainingText = Array.from(el.childNodes)
			.filter((n) => n.nodeType === Node.TEXT_NODE)
			.map((n) => n.textContent)
			.join('')
			.trim();
		return { el, emoteOnly: emoteCount > 0 && remainingText.length === 0 };
	}

	function buildMessage(msg, emotes) {
		const layout = design.layout;
		const user = msg.user || {};
		const color = nameColor(msg);

		const root = document.createElement('div');
		root.className = 'sf-message sf-platform-' + msg.platform;
		for (const role of user.roles || [])
			root.classList.add('sf-role-' + role);
		if (msg.isAction)
			root.classList.add('sf-action');
		root.dataset.id = msg.id || '';
		root.dataset.userId = user.id || '';
		root.style.setProperty('--sf-user-color', color);
		root.style.setProperty('--sf-accent-color',
			design.bubble.accent === 'user' ? color : SF.platformColors[msg.platform] || color);

		const highlight = pickHighlight(msg);
		if (highlight) {
			root.classList.add('sf-highlighted', 'sf-hl-' + highlight.key);
			if (highlight.key === 'firstTime')
				root.classList.add('sf-first-time');
			if (highlight.key === 'mention')
				root.classList.add('sf-mention');
			root.style.setProperty('--sf-highlight-color', SF.color(highlight.color));
			root.style.setProperty('--sf-highlight-tint', SF.color(highlight.color.slice(0, 7) + '40'));
		}

		const bubble = document.createElement('div');
		bubble.className = 'sf-bubble';
		root.appendChild(bubble);

		if (msg.reply && msg.reply.user) {
			const reply = document.createElement('span');
			reply.className = 'sf-reply';
			reply.textContent = `↪ @${msg.reply.user}: ${msg.reply.text || ''}`;
			bubble.appendChild(reply);
		}

		const meta = document.createElement('span');
		meta.className = 'sf-meta';
		bubble.appendChild(meta);

		if (layout.showTimestamps) {
			const time = document.createElement('span');
			time.className = 'sf-time';
			time.textContent = new Date(msg.timestamp || Date.now()).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
			meta.appendChild(time);
		}
		if (layout.showAvatars && user.avatar) {
			const avatar = document.createElement('img');
			avatar.className = 'sf-avatar';
			avatar.src = user.avatar;
			meta.appendChild(avatar);
		}
		if (layout.showPlatformIcon)
			meta.appendChild(SF.platformIcon(msg.platform));
		if (layout.showBadges && user.badges && user.badges.some((b) => b.url)) {
			const badges = document.createElement('span');
			badges.className = 'sf-badges';
			for (const badge of user.badges) {
				if (!badge.url)
					continue;
				const img = document.createElement('img');
				img.className = 'sf-badge';
				img.src = badge.url;
				img.title = badge.title || badge.id;
				badges.appendChild(img);
			}
			meta.appendChild(badges);
		}
		if (highlight && highlight.key === 'firstTime') {
			const label = document.createElement('span');
			label.className = 'sf-first-time-label';
			label.textContent = 'First message';
			meta.appendChild(label);
		}

		const name = document.createElement('span');
		name.className = 'sf-name';
		name.textContent = user.displayName || user.login || '';
		name.style.color = color;
		meta.appendChild(name);

		if (design.highlights.pronouns && msg.platform === 'twitch' && user.login) {
			const pronouns = document.createElement('span');
			pronouns.className = 'sf-pronouns';
			pronouns.hidden = true;
			meta.appendChild(pronouns);
			SF.pronouns(user.login).then((p) => {
				if (p) {
					pronouns.textContent = p;
					pronouns.hidden = false;
				}
			});
		}

		if (layout.nameSuffix && !msg.isAction) {
			const suffix = document.createElement('span');
			suffix.className = 'sf-suffix';
			suffix.textContent = layout.nameSuffix;
			suffix.style.marginLeft = '0';
			meta.appendChild(suffix);
		}

		if (msg.paid) {
			/* YouTube Super Chat / Super Sticker amount */
			const paid = document.createElement('span');
			paid.className = 'sf-paid-label';
			paid.textContent = msg.paid;
			meta.appendChild(paid);
		}

		const text = buildText(msg, emotes);
		bubble.appendChild(text.el);
		return { root, emoteOnly: text.emoteOnly };
	}

	/* ---- lifetime ---- */

	function messages() {
		return Array.from(feed.children).filter((el) => !el.dataset.removing);
	}

	function oldestFirst() {
		const list = messages();
		return design.layout.direction === 'newest-top' ? list.reverse() : list;
	}

	async function removeMessage(el, animated) {
		if (!el || el.dataset.removing)
			return;
		el.dataset.removing = '1';
		clearTimeout(el._sfExpire);
		const a = design.animation;
		if (animated !== false)
			await SF.animate(el, 'out', a.out, a.outDuration, a.outEasing);
		const play = a.reflow ? SF.flip(messages(), 250) : null;
		el.remove();
		if (play)
			play();
	}

	function enforceLimits() {
		if (!design)
			return;
		const max = design.lifetime.maxMessages;
		if (max > 0) {
			const list = oldestFirst();
			for (let i = 0; i < list.length - max; i++)
				removeMessage(list[i], true);
		}
		if (design.lifetime.removeOverflow)
			requestAnimationFrame(removeOverflowing);
	}

	function removeOverflowing() {
		/* Layout offsets, not getBoundingClientRect(): FLIP transforms would report the old
		 * on-screen positions while messages are still sliding into place. */
		const height = feed.clientHeight;
		const top = design.layout.direction !== 'newest-top';
		for (const el of messages()) {
			const start = el.offsetTop;
			const end = start + el.offsetHeight;
			const fullyOut = top ? end <= 0 : start >= height;
			const partlyOut = top ? start < -1 : end > height + 1;
			if (fullyOut)
				removeMessage(el, false);
			else if (partlyOut)
				removeMessage(el, true);
		}
	}

	function addMessage(el) {
		const a = design.animation;
		const play = a.reflow ? SF.flip(messages(), a.inDuration) : null;
		if (design.layout.direction === 'newest-top')
			feed.prepend(el);
		else
			feed.appendChild(el);
		if (play)
			play();
		SF.animate(el, 'in', a.in, a.inDuration, a.inEasing);

		const expire = design.lifetime.expireSeconds;
		if (expire > 0)
			el._sfExpire = setTimeout(() => removeMessage(el, true), expire * 1000);
		enforceLimits();
	}

	function withTimeout(promise, ms) {
		return Promise.race([promise, SF.sleep(ms).then(() => null)]);
	}

	async function handleChat(msg) {
		if (!design || !passesFilters(msg))
			return;
		let emotes = null;
		const opts = design.emotes;
		if (msg.platform === 'twitch' && (opts.bttv || opts.ffz || opts.sevenTv))
			emotes = await withTimeout(SF.emotes.forChannel(msg.channelId || '', opts), 1500);
		const built = buildMessage(msg, emotes);
		if (built.emoteOnly && design.filters.hideEmoteOnly)
			return;
		addMessage(built.root);
	}

	function handleModeration(m) {
		if (m.action === 'clear_chat') {
			for (const el of messages())
				removeMessage(el, true);
			return;
		}
		const attr = m.action === 'delete_message' ? 'id' : 'userId';
		for (const el of messages()) {
			if (m.target && el.dataset[attr] === m.target)
				removeMessage(el, true);
		}
	}

	SF.on('config', (config) => applyDesign(config.design));
	SF.on('chat', (msg) => {
		/* Keep arrival order even when emote sets load asynchronously. */
		queue = queue.then(() => handleChat(msg)).catch((err) => console.error(err));
	});
	SF.on('moderation', handleModeration);
	window.addEventListener('resize', () => enforceLimits());

	SF.start();
})();
