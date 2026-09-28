/* Social Feed: Event Display. Shows one event at a time from a priority queue. */
(function () {
	'use strict';

	const SF = window.SF;
	const stage = document.getElementById('stage');
	let design = null;
	let pending = [];
	let showing = false;
	let seq = 0;

	/* ---- design ---- */

	function applyDesign(d) {
		design = d;
		const l = d.layout, t = d.text, c = d.card;
		document.body.className = [
			'sf-h-' + l.horizontal,
			'sf-v-' + l.vertical,
			'sf-style-' + l.style,
			'sf-media-' + l.mediaPosition,
			'sf-textanim-' + (t.textAnimation || 'none'),
		].join(' ');
		SF.setVars(document.documentElement, {
			'--sf-font': t.fontFamily,
			'--sf-title-size': t.titleSize + 'px',
			'--sf-title-weight': t.titleWeight,
			'--sf-title-color': SF.color(t.titleColor),
			'--sf-highlight-color': SF.color(t.highlightColor),
			'--sf-message-size': t.messageSize + 'px',
			'--sf-message-color': SF.color(t.messageColor),
			'--sf-text-shadow': t.shadow ? `0 2px 6px ${SF.color(t.shadowColor)}` : 'none',
			'--sf-card-bg': SF.color(c.background),
			'--sf-card-radius': c.radius + 'px',
			'--sf-card-border-width': c.borderWidth + 'px',
			'--sf-card-border-color': SF.color(c.borderColor),
			'--sf-padding': l.padding + 'px',
			'--sf-gap': l.gap + 'px',
			'--sf-media-size': l.mediaSize + 'px',
			'--sf-max-width': l.maxWidth + '%',
			'--sf-text-align': l.textAlign,
		});
	}

	/* ---- queue ---- */

	function typeSettings(type) {
		return (design.types && design.types[type]) || null;
	}

	function amountOf(event) {
		const n = Number(event.amount != null ? event.amount : event.count);
		return isFinite(n) ? n : 0;
	}

	function accept(event) {
		const settings = typeSettings(event.type);
		if (!settings || !settings.enabled)
			return false;
		if (event.test)
			return true;
		if (design.platforms && design.platforms[event.platform] === false)
			return false;
		return amountOf(event) >= (Number(settings.minAmount) || 0);
	}

	function enqueue(event) {
		if (!design || !accept(event))
			return;
		const item = { event, priority: Number(typeSettings(event.type).priority) || 0, seq: seq++ };
		const max = Math.max(1, design.queue.maxQueue || 1);

		if (pending.length >= max) {
			if (!design.queue.dropLowestWhenFull)
				return;
			/* Drop the newest of the lowest-priority items, if the new one outranks it. */
			let lowest = 0;
			for (let i = 1; i < pending.length; i++) {
				const a = pending[i], b = pending[lowest];
				if (a.priority < b.priority || (a.priority === b.priority && a.seq > b.seq))
					lowest = i;
			}
			if (pending[lowest].priority >= item.priority)
				return;
			pending.splice(lowest, 1);
		}
		pending.push(item);
		pump();
	}

	function takeNext() {
		let best = 0;
		for (let i = 1; i < pending.length; i++) {
			const a = pending[i], b = pending[best];
			if (a.priority > b.priority || (a.priority === b.priority && a.seq < b.seq))
				best = i;
		}
		return pending.splice(best, 1)[0];
	}

	async function pump() {
		if (showing || !pending.length)
			return;
		showing = true;
		try {
			while (pending.length) {
				await show(takeNext().event);
				await SF.sleep((Number(design.queue.gapSeconds) || 0) * 1000);
			}
		} finally {
			showing = false;
		}
	}

	/* ---- rendering ---- */

	function resolve(event) {
		const base = typeSettings(event.type);
		const amount = amountOf(event);
		let variant = null;
		for (const v of base.variants || []) {
			if (amount >= (Number(v.minAmount) || 0) && (!variant || Number(v.minAmount) > Number(variant.minAmount)))
				variant = v;
		}
		const pick = (key) => (variant && variant[key] ? variant[key] : base[key]);
		return {
			title: pick('title') || '',
			message: pick('message') || '',
			mediaUrl: (variant && variant.mediaUrl) || base.mediaUrl || '',
			soundUrl: (variant && variant.soundUrl) || base.soundUrl || '',
			volume: base.volume,
			tts: base.tts,
			hold: Number(base.holdSeconds) > 0 ? Number(base.holdSeconds) : Number(design.queue.holdSeconds) || 6,
		};
	}

	function tokens(event) {
		const user = event.user || {};
		const amount = event.amount != null ? event.amount : event.count;
		return {
			name: user.displayName || user.login || 'Someone',
			amount: amount != null ? String(amount) : '',
			formattedAmount: event.formattedAmount || (amount != null ? String(amount) : ''),
			count: event.count != null ? String(event.count) : amount != null ? String(amount) : '',
			months: event.months != null ? String(event.months) : '',
			tier: SF.tierLabel(event.tier),
			reward: event.reward || '',
			recipient: event.recipient || '',
			message: event.message || '',
			platform: SF.platformLabels[event.platform] || event.platform || '',
		};
	}

	const HIGHLIGHTED = new Set(['name', 'amount', 'formattedAmount', 'count', 'months', 'tier', 'reward', 'recipient']);

	function letters(text) {
		return Array.from(text)
			.map((ch, i) => `<span class="sf-letter" style="animation-delay:${(i * 0.06).toFixed(2)}s">${SF.escape(ch)}</span>`)
			.join('');
	}

	function fillTemplate(template, values, highlight) {
		const perLetter = ['wave', 'bounce'].includes(design.text.textAnimation);
		return SF.escape(template).replace(/\{(\w+)\}/g, (match, key) => {
			if (!(key in values))
				return match;
			const value = values[key];
			if (!highlight || !HIGHLIGHTED.has(key) || !value)
				return SF.escape(value);
			const inner = perLetter ? letters(value) : SF.escape(value);
			return `<span class="sf-highlight">${inner}</span>`;
		});
	}

	function buildMedia(url, volume) {
		const box = document.createElement('div');
		box.className = 'sf-media';
		let element;
		if (SF.isVideo(url)) {
			element = document.createElement('video');
			element.src = url;
			element.autoplay = true;
			element.playsInline = true;
			element.volume = Math.min(1, Math.max(0, (volume == null ? 70 : volume) / 100));
		} else {
			element = document.createElement('img');
			element.src = url;
		}
		box.appendChild(element);
		return { box, element };
	}

	function playAudio(url, volume) {
		return new Promise((resolve) => {
			if (!url)
				return resolve();
			const audio = new Audio(url);
			audio.volume = Math.min(1, Math.max(0, (volume == null ? 70 : volume) / 100));
			const done = () => resolve();
			audio.addEventListener('ended', done);
			audio.addEventListener('error', done);
			audio.play().catch(done);
			/* Never let a stuck file block the queue. */
			setTimeout(done, 30000);
		});
	}

	function ttsText(event, resolved) {
		const tts = design.tts;
		if (!tts.enabled || !resolved.tts)
			return '';
		const message = String(event.message || '').trim();
		if (!message || amountOf(event) < (Number(tts.minAmount) || 0))
			return '';
		const lower = message.toLowerCase();
		if ((tts.blockedWords || []).some((w) => w && lower.includes(String(w).toLowerCase())))
			return '';
		let text = message.slice(0, Math.max(10, tts.maxLength || 200));
		if (tts.readName)
			text = `${tokens(event).name} says: ${text}`;
		return text;
	}

	function speakWithBrowser(text, volume) {
		return new Promise((resolve) => {
			if (!window.speechSynthesis)
				return resolve();
			const utterance = new SpeechSynthesisUtterance(text);
			utterance.volume = volume;
			utterance.onend = resolve;
			utterance.onerror = resolve;
			speechSynthesis.speak(utterance);
			setTimeout(resolve, 60000);
		});
	}

	/* StreamElements' public TTS endpoint returns an mp3 that plays through the browser audio
	 * (and therefore through OBS). Falls back to the browser's speech synthesis. */
	async function speak(text) {
		const tts = design.tts;
		const volume = Math.min(1, Math.max(0, (tts.volume == null ? 80 : tts.volume) / 100));
		const url = 'https://api.streamelements.com/kappa/v2/speech?voice=' + encodeURIComponent(tts.voice || 'Brian') +
			'&text=' + encodeURIComponent(text);
		const ok = await new Promise((resolve) => {
			const audio = new Audio(url);
			audio.volume = volume;
			let started = false;
			audio.addEventListener('playing', () => (started = true));
			audio.addEventListener('ended', () => resolve(true));
			audio.addEventListener('error', () => resolve(started));
			audio.play().catch(() => resolve(false));
			setTimeout(() => resolve(true), 60000);
		});
		if (!ok)
			await speakWithBrowser(text, volume);
	}

	async function show(event) {
		const resolved = resolve(event);
		const values = tokens(event);
		const a = design.animation;

		const alert = document.createElement('div');
		alert.className = `sf-alert sf-type-${event.type} sf-platform-${event.platform}`;
		const card = document.createElement('div');
		card.className = 'sf-card';
		alert.appendChild(card);

		let media = null;
		if (resolved.mediaUrl && design.layout.mediaPosition !== 'none') {
			media = buildMedia(resolved.mediaUrl, resolved.volume);
			card.appendChild(media.box);
		}

		const body = document.createElement('div');
		body.className = 'sf-body';
		const title = document.createElement('div');
		title.className = 'sf-title';
		title.innerHTML = fillTemplate(resolved.title, values, true);
		const message = document.createElement('div');
		message.className = 'sf-message';
		message.innerHTML = fillTemplate(resolved.message, values, false);
		body.appendChild(title);
		body.appendChild(message);
		card.appendChild(body);

		stage.replaceChildren(alert);
		const entered = SF.animate(alert, 'in', a.in, a.inDuration, a.inEasing);

		const holdMs = resolved.hold * 1000;
		const tasks = [SF.sleep(holdMs)];

		const speech = ttsText(event, resolved);
		const sound = playAudio(resolved.soundUrl, resolved.volume);
		tasks.push(sound);
		if (speech) {
			tasks.push(
				(resolved.soundUrl ? sound : Promise.resolve())
					.then(() => SF.sleep(design.tts.delayMs || 0))
					.then(() => speak(speech)));
		}
		if (media && media.element.tagName === 'VIDEO') {
			tasks.push(new Promise((done) => {
				media.element.addEventListener('ended', done);
				media.element.addEventListener('error', done);
				setTimeout(done, 60000);
			}));
		}

		await entered;
		await Promise.all(tasks);
		await SF.animate(alert, 'out', a.out, a.outDuration, a.outEasing);
		alert.remove();
	}

	SF.on('config', (config) => applyDesign(config.design));
	SF.on('event', enqueue);

	SF.start();
})();
