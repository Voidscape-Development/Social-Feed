/* Demo mode for the editor preview: feeds sample chat messages / events while config.demo is
 * true. The plugin only enables demo on the editor's private preview copy of a source. */
(function () {
	'use strict';

	const SF = window.SF;

	const users = [
		{ displayName: 'PixelPanda', color: '#FF7F50', roles: ['subscriber'] },
		{ displayName: 'NightOwl_42', color: '#1E90FF', roles: [] },
		{ displayName: 'ModMaverick', color: '#00C853', roles: ['moderator'] },
		{ displayName: 'VIPVelvet', color: '#E040FB', roles: ['vip'] },
		{ displayName: 'cozy_coder', color: '#FFD600', roles: [] },
		{ displayName: 'GlitchGoblin', color: '#FF1744', roles: ['subscriber'] },
		{ displayName: 'StreamerName', color: '#9146FF', roles: ['broadcaster'] },
	];

	const messages = [
		'Hello chat! Kappa',
		'That play was insane PogChamp',
		'This overlay looks so clean',
		'GG everyone, see you next stream',
		'LUL LUL LUL',
		'Can we get some hype in the chat?',
		'This song is a vibe 🎶',
		'Anyone else here since the early days?',
	];

	const platforms = ['twitch', 'youtube', 'kick', 'tiktok'];
	const pick = (list) => list[Math.floor(Math.random() * list.length)];
	let seq = 0;

	function demoUser() {
		const u = pick(users);
		return Object.assign({ id: 'demo-' + u.displayName, login: u.displayName.toLowerCase(), badges: [] }, u);
	}

	function fragments(text) {
		const out = [];
		for (const word of text.split(' ')) {
			if (out.length)
				out.push({ type: 'text', text: ' ' });
			if (word === 'Kappa')
				out.push({ type: 'emote', text: word, url: 'https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/2.0' });
			else if (word === 'LUL')
				out.push({ type: 'emote', text: word, url: 'https://static-cdn.jtvnw.net/emoticons/v2/425618/default/dark/2.0' });
			else
				out.push({ type: 'text', text: word });
		}
		return out;
	}

	SF.demoChat = function () {
		const text = pick(messages);
		return {
			id: 'demo-' + ++seq,
			platform: pick(platforms),
			channel: 'demo',
			user: demoUser(),
			text,
			fragments: fragments(text),
			firstMessage: Math.random() < 0.1,
			timestamp: Date.now(),
			test: true,
		};
	};

	const eventTemplates = [
		{ type: 'follow', platform: 'twitch' },
		{ type: 'subscription', platform: 'twitch', tier: '1000', months: 7, amount: 7, message: 'Seven months already!' },
		{ type: 'gift_sub', platform: 'twitch', tier: '1000', count: 5, amount: 5 },
		{ type: 'cheer', platform: 'twitch', amount: 500, formattedAmount: '500 bits', message: 'Cheer500 love the stream' },
		{ type: 'tip', platform: 'streamelements', amount: 10, currency: 'USD', formattedAmount: '$10.00', message: 'Keep it up!' },
		{ type: 'raid', platform: 'twitch', count: 42, amount: 42 },
		{ type: 'redemption', platform: 'twitch', reward: 'Hydrate!', amount: 500, formattedAmount: '500 points' },
		{ type: 'super_chat', platform: 'youtube', amount: 20, currency: 'USD', formattedAmount: '$20.00', message: 'Greetings from YouTube' },
		{ type: 'membership', platform: 'youtube', tier: 'Member', months: 1, amount: 1 },
		{ type: 'gift', platform: 'tiktok', reward: 'Rose', count: 12, amount: 12 },
	];
	let eventIndex = 0;

	SF.demoEvent = function (type) {
		let template = eventTemplates[eventIndex++ % eventTemplates.length];
		if (type)
			template = eventTemplates.find((t) => t.type === type) || { type, platform: 'twitch' };
		return Object.assign({ id: 'demo-' + ++seq, channel: 'demo', user: demoUser(), timestamp: Date.now(), test: true }, template);
	};

	let timer = null;
	SF.on('config', (config) => {
		const wanted = !!config.demo;
		if (wanted && !timer) {
			const tick = () => {
				if (config.kind === 'chat')
					SF.emit('chat', SF.demoChat());
				else
					SF.emit('event', SF.demoEvent());
			};
			tick();
			timer = setInterval(tick, config.kind === 'chat' ? 1800 : 9000);
		} else if (!wanted && timer) {
			clearInterval(timer);
			timer = null;
		}
	});
})();
