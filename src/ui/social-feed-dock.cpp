/*
Social Feed
Copyright (C) 2026 Voidscape Development

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "ui/social-feed-dock.hpp"

#include "core/config-store.hpp"
#include "core/event-bus.hpp"
#include "core/sample-data.hpp"
#include "providers/provider-manager.hpp"
#include "providers/twitch/twitch-auth.hpp"
#include "providers/youtube/youtube-api.hpp"
#include "providers/youtube/youtube-chat-stream.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QThread>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <chrono>
#include <thread>

namespace sf::ui {

/* ---- DeviceLoginDialog ---- */

DeviceLoginDialog::DeviceLoginDialog(DeviceFlow deviceFlow, QWidget *parent)
	: QDialog(parent),
	  flow(std::move(deviceFlow)),
	  cancelled(std::make_shared<std::atomic<bool>>(false))
{
	setWindowTitle(tr("Log in with %1").arg(flow.platform));
	setMinimumWidth(420);

	auto *layout = new QVBoxLayout(this);
	auto *intro = new QLabel(tr("Open the %1 activation page and enter this code:").arg(flow.platform));
	intro->setWordWrap(true);
	layout->addWidget(intro);

	codeLabel = new QLabel(QStringLiteral("…"));
	QFont font = codeLabel->font();
	font.setPointSize(font.pointSize() * 2);
	font.setBold(true);
	font.setLetterSpacing(QFont::AbsoluteSpacing, 4);
	codeLabel->setFont(font);
	codeLabel->setAlignment(Qt::AlignCenter);
	codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(codeLabel);

	openButton = new QPushButton(tr("Open the activation page"));
	openButton->setEnabled(false);
	layout->addWidget(openButton);
	connect(openButton, &QPushButton::clicked, this,
		[this]() { QDesktopServices::openUrl(QUrl(verificationUri)); });

	statusLabel = new QLabel(tr("Requesting a code from %1…").arg(flow.platform));
	statusLabel->setWordWrap(true);
	statusLabel->setOpenExternalLinks(true);
	layout->addWidget(statusLabel);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	start();
}

DeviceLoginDialog::~DeviceLoginDialog()
{
	*cancelled = true;
}

void DeviceLoginDialog::showCode(const QString &userCode, const QString &uri)
{
	codeLabel->setText(userCode);
	verificationUri = uri;
	openButton->setText(tr("Open %1").arg(QUrl(uri).host() + QUrl(uri).path()));
	openButton->setEnabled(true);
	statusLabel->setText(tr("Waiting for you to approve the login on %1…").arg(flow.platform));
	QDesktopServices::openUrl(QUrl(uri));
}

void DeviceLoginDialog::fail(const QString &message)
{
	statusLabel->setText(message);
	openButton->setEnabled(false);
}

void DeviceLoginDialog::start()
{
	if (!flow.missingSetupError.isEmpty()) {
		fail(flow.missingSetupError);
		return;
	}

	QPointer<DeviceLoginDialog> self(this);
	auto cancel = cancelled;
	DeviceFlow f = flow;
	std::thread([self, cancel, f]() {
		auto post = [&](auto fn) {
			QMetaObject::invokeMethod(
				qApp,
				[self, fn]() {
					if (self)
						fn(self.data());
				},
				Qt::QueuedConnection);
		};

		oauth::DeviceCode code = f.requestCode();
		if (!code.ok) {
			post([code, f](DeviceLoginDialog *d) {
				d->fail(tr("%1 refused the login request: %2").arg(f.platform, code.error));
			});
			return;
		}
		post([code](DeviceLoginDialog *d) { d->showCode(code.userCode, code.verificationUri); });

		int interval = code.intervalSeconds;
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(code.expiresInSeconds);
		while (!*cancel && std::chrono::steady_clock::now() < deadline) {
			for (int i = 0; i < interval * 10 && !*cancel; i++)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (*cancel)
				return;

			oauth::TokenResult token = f.poll(code.deviceCode);
			switch (token.status) {
			case oauth::TokenResult::Status::Pending:
				continue;
			case oauth::TokenResult::Status::SlowDown:
				interval += 5;
				continue;
			case oauth::TokenResult::Status::Success: {
				if (*cancel)
					return;
				QString error = f.finish(token);
				if (error.isEmpty())
					post([](DeviceLoginDialog *d) { d->accept(); });
				else
					post([error](DeviceLoginDialog *d) {
						d->fail(tr("Login failed: %1").arg(error));
					});
				return;
			}
			case oauth::TokenResult::Status::Denied:
				post([f](DeviceLoginDialog *d) {
					d->fail(tr("The login was declined on %1.").arg(f.platform));
				});
				return;
			case oauth::TokenResult::Status::Expired:
				post([](DeviceLoginDialog *d) {
					d->fail(tr("The code expired. Close this window and try again."));
				});
				return;
			case oauth::TokenResult::Status::Error:
				post([token](DeviceLoginDialog *d) {
					d->fail(tr("Login failed: %1").arg(token.error));
				});
				return;
			}
		}
		post([](DeviceLoginDialog *d) { d->fail(tr("The code expired. Close this window and try again.")); });
	}).detach();
}

namespace {

DeviceFlow twitchFlow()
{
	DeviceFlow flow;
	flow.platform = QStringLiteral("Twitch");
	QString clientId = twitch::clientId();
	if (clientId.isEmpty())
		flow.missingSetupError = QObject::tr(
			"No Twitch Client ID is configured. Register an application at dev.twitch.tv (client type "
			"\"Public\") and enter its Client ID under Twitch → Advanced.");
	flow.requestCode = [clientId]() {
		return twitch::requestDeviceCode(clientId);
	};
	flow.poll = [clientId](const QString &deviceCode) {
		return twitch::pollDeviceToken(clientId, deviceCode);
	};
	flow.finish = [](const oauth::TokenResult &token) {
		twitch::storeLogin(token, twitch::validateToken(token.accessToken));
		return QString();
	};
	return flow;
}

DeviceFlow youtubeFlow()
{
	DeviceFlow flow;
	flow.platform = QStringLiteral("Google");
	youtube::AppCredentials app = youtube::appCredentials();
	if (!app.valid())
		flow.missingSetupError =
			QObject::tr("Enter the Client ID and Client secret of your Google OAuth client under YouTube → "
				    "Advanced first. See the Social Feed README for how to create one.");
	flow.requestCode = [app]() {
		return youtube::requestDeviceCode(app);
	};
	flow.poll = [app](const QString &deviceCode) {
		return youtube::pollDeviceToken(app, deviceCode);
	};
	flow.finish = [](const oauth::TokenResult &token) {
		youtube::ChannelInfo channel = youtube::fetchOwnChannel(token.accessToken);
		if (!channel.ok)
			return channel.error;
		youtube::storeLogin(token, channel);
		return QString();
	};
	return flow;
}

} // namespace

/* ---- SocialFeedDock ---- */

SocialFeedDock::SocialFeedDock(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	auto *tabs = new QTabWidget(this);
	tabs->addTab(buildAccountsTab(), tr("Accounts"));
	tabs->addTab(buildHistoryTab(), tr("Events"));
	tabs->addTab(buildTestTab(), tr("Test"));
	layout->addWidget(tabs);

	auto &manager = ProviderManager::instance();
	connect(&manager, &ProviderManager::statusChanged, this, &SocialFeedDock::refreshStatus);
	for (const auto &provider : manager.providers())
		refreshStatus(provider->id());

	connect(&ConfigStore::instance(), &ConfigStore::sectionChanged, this, [this](const QString &section) {
		if (section == "twitch")
			refreshTwitchAccount();
		else if (section == "youtube")
			refreshYouTubeAccount();
	});
	refreshTwitchAccount();
	refreshYouTubeAccount();

	connect(&EventBus::instance(), &EventBus::historyChanged, this, &SocialFeedDock::refreshHistory,
		Qt::QueuedConnection);
	refreshHistory();
}

QWidget *SocialFeedDock::buildAccountsTab()
{
	auto *scroll = new QScrollArea();
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget();
	auto *layout = new QVBoxLayout(content);

	layout->addWidget(buildTwitchBox());
	layout->addWidget(buildYouTubeBox());

	/* Everything else, not implemented yet */
	for (const auto &provider : ProviderManager::instance().providers()) {
		if (provider->id() == "twitch" || provider->id() == "youtube")
			continue;
		auto *box = new QGroupBox(provider->displayName());
		auto *boxLayout = new QVBoxLayout(box);
		auto *label = new QLabel();
		label->setWordWrap(true);
		statusLabels[provider->id()] = label;
		boxLayout->addWidget(label);
		layout->addWidget(box);
	}

	layout->addStretch();
	scroll->setWidget(content);
	return scroll;
}

namespace {

/* Collapsible "Advanced" group; returns the layout to fill. */
QVBoxLayout *addAdvancedGroup(QGridLayout *grid, int row)
{
	auto *advanced = new QGroupBox(QObject::tr("Advanced"));
	advanced->setCheckable(true);
	advanced->setChecked(false);
	auto *advLayout = new QVBoxLayout(advanced);
	auto *advWidget = new QWidget();
	auto *inner = new QVBoxLayout(advWidget);
	inner->setContentsMargins(0, 0, 0, 0);
	advLayout->addWidget(advWidget);
	advWidget->setVisible(false);
	QObject::connect(advanced, &QGroupBox::toggled, advWidget, &QWidget::setVisible);
	grid->addWidget(advanced, row, 0, 1, 2);
	return inner;
}

} // namespace

QWidget *SocialFeedDock::buildTwitchBox()
{
	auto *box = new QGroupBox(tr("Twitch"));
	auto *grid = new QGridLayout(box);
	twitchAccount = new QLabel();
	twitchAccount->setWordWrap(true);
	statusLabels["twitch"] = new QLabel();
	statusLabels["twitch"]->setWordWrap(true);
	twitchLogin = new QPushButton(tr("Log in…"));
	twitchLogout = new QPushButton(tr("Log out"));
	twitchEvents = new QCheckBox(tr("Receive alerts (follows, subs, bits, raids, redemptions)"));
	grid->addWidget(twitchAccount, 0, 0, 1, 2);
	grid->addWidget(statusLabels["twitch"], 1, 0, 1, 2);
	grid->addWidget(twitchLogin, 2, 0);
	grid->addWidget(twitchLogout, 2, 1);
	grid->addWidget(twitchEvents, 3, 0, 1, 2);

	QVBoxLayout *advanced = addAdvancedGroup(grid, 4);
	twitchClientId = new QLineEdit();
	twitchClientId->setPlaceholderText(QString::fromUtf8(SOCIAL_FEED_TWITCH_CLIENT_ID).isEmpty()
						   ? tr("Twitch application Client ID")
						   : tr("Built-in Client ID"));
	advanced->addWidget(new QLabel(tr("Client ID override:")));
	advanced->addWidget(twitchClientId);

	connect(twitchLogin, &QPushButton::clicked, this, [this]() {
		DeviceLoginDialog dialog(twitchFlow(), this);
		dialog.exec();
	});
	connect(twitchLogout, &QPushButton::clicked, this, [this]() {
		if (QMessageBox::question(this, tr("Log out"), tr("Log out of Twitch?")) != QMessageBox::Yes)
			return;
		QString token = ConfigStore::instance().section("twitch").value("accessToken").toString();
		QString clientId = twitch::clientId();
		if (!token.isEmpty() && !clientId.isEmpty())
			std::thread([clientId, token]() { twitch::revokeToken(clientId, token); }).detach();
		twitch::clearLogin();
	});
	connect(twitchEvents, &QCheckBox::toggled, this,
		[](bool on) { ConfigStore::instance().updateSection("twitch", {{"eventsEnabled", on}}); });
	connect(twitchClientId, &QLineEdit::editingFinished, this, [this]() {
		ConfigStore::instance().updateSection("twitch", {{"clientId", twitchClientId->text().trimmed()}});
	});
	return box;
}

QWidget *SocialFeedDock::buildYouTubeBox()
{
	auto *box = new QGroupBox(tr("YouTube"));
	auto *grid = new QGridLayout(box);
	youtubeAccount = new QLabel();
	youtubeAccount->setWordWrap(true);
	statusLabels["youtube"] = new QLabel();
	statusLabels["youtube"]->setWordWrap(true);
	youtubeLogin = new QPushButton(tr("Log in…"));
	youtubeLogout = new QPushButton(tr("Log out"));
	grid->addWidget(youtubeAccount, 0, 0, 1, 2);
	grid->addWidget(statusLabels["youtube"], 1, 0, 1, 2);
	grid->addWidget(youtubeLogin, 2, 0);
	grid->addWidget(youtubeLogout, 2, 1);

	auto *intervalRow = new QHBoxLayout();
	youtubePoll = new QSpinBox();
	youtubePoll->setRange(3, 120);
	youtubePoll->setSuffix(tr(" s"));
	youtubePoll->setToolTip(tr("How often live chat is fetched. Faster costs more of the daily quota."));
	intervalRow->addWidget(new QLabel(tr("Chat refresh every")));
	intervalRow->addWidget(youtubePoll);
	intervalRow->addStretch();
	grid->addLayout(intervalRow, 3, 0, 1, 2);
	youtubeEstimate = new QLabel();
	youtubeEstimate->setWordWrap(true);
	youtubeEstimate->setStyleSheet("color: gray;");
	grid->addWidget(youtubeEstimate, 4, 0, 1, 2);

	youtubeStreaming = new QCheckBox(tr("Use streaming chat (experimental)"));
	youtubeStreaming->setToolTip(
		tr("Keep one liveChatMessages.streamList connection open instead of polling. Every connection is "
		   "logged to youtube-stream-log.csv so its real quota cost can be measured."));
	grid->addWidget(youtubeStreaming, 5, 0, 1, 2);

	QVBoxLayout *advanced = addAdvancedGroup(grid, 6);
	auto *help = new QLabel(
		tr("YouTube uses your own Google Cloud project, so the API quota is yours. Create a project, enable "
		   "the <i>YouTube Data API v3</i>, and add an OAuth client of type <i>TVs and Limited Input "
		   "devices</i>. <a href=\"https://console.cloud.google.com/apis/credentials\">Open Google Cloud "
		   "credentials</a>"));
	help->setWordWrap(true);
	help->setOpenExternalLinks(true);
	advanced->addWidget(help);
	youtubeClientId = new QLineEdit();
	youtubeClientId->setPlaceholderText(tr("…apps.googleusercontent.com"));
	youtubeClientSecret = new QLineEdit();
	youtubeClientSecret->setEchoMode(QLineEdit::Password);
	youtubeQuota = new QSpinBox();
	youtubeQuota->setRange(100, 10000000);
	youtubeQuota->setSingleStep(1000);
	youtubeQuota->setSuffix(tr(" units/day"));
	advanced->addWidget(new QLabel(tr("Client ID:")));
	advanced->addWidget(youtubeClientId);
	advanced->addWidget(new QLabel(tr("Client secret:")));
	advanced->addWidget(youtubeClientSecret);
	advanced->addWidget(new QLabel(tr("Daily quota of your project:")));
	advanced->addWidget(youtubeQuota);
	youtubeStreamCost = new QSpinBox();
	youtubeStreamCost->setRange(0, 100);
	youtubeStreamCost->setSuffix(tr(" units"));
	youtubeStreamCost->setToolTip(tr("Google does not document the cost of a streaming connection. This value is "
					 "only used for the plugin's own budget; compare the log with the quota graph "
					 "in Google Cloud Console to find the real number."));
	advanced->addWidget(new QLabel(tr("Assumed cost per streaming connection:")));
	advanced->addWidget(youtubeStreamCost);
	auto *openLog = new QPushButton(tr("Open streaming measurement log"));
	advanced->addWidget(openLog);
	connect(openLog, &QPushButton::clicked, this, []() {
		QString path = youtube::streamLogPath();
		QFileInfo info(path);
		QDesktopServices::openUrl(QUrl::fromLocalFile(info.exists() ? path : info.absolutePath()));
	});

	connect(youtubeLogin, &QPushButton::clicked, this, [this]() {
		DeviceLoginDialog dialog(youtubeFlow(), this);
		dialog.exec();
	});
	connect(youtubeLogout, &QPushButton::clicked, this, [this]() {
		if (QMessageBox::question(this, tr("Log out"), tr("Log out of YouTube?")) != QMessageBox::Yes)
			return;
		QString token = ConfigStore::instance().section("youtube").value("refreshToken").toString();
		if (!token.isEmpty())
			std::thread([token]() { youtube::revokeToken(token); }).detach();
		youtube::clearLogin();
	});
	connect(youtubeClientId, &QLineEdit::editingFinished, this, [this]() {
		ConfigStore::instance().updateSection("youtube", {{"clientId", youtubeClientId->text().trimmed()}});
	});
	connect(youtubeClientSecret, &QLineEdit::editingFinished, this, [this]() {
		ConfigStore::instance().updateSection("youtube",
						      {{"clientSecret", youtubeClientSecret->text().trimmed()}});
	});
	connect(youtubePoll, &QSpinBox::valueChanged, this,
		[](int seconds) { ConfigStore::instance().updateSection("youtube", {{"pollSeconds", seconds}}); });
	connect(youtubeQuota, &QSpinBox::valueChanged, this,
		[](int units) { ConfigStore::instance().updateSection("youtube", {{"dailyQuota", units}}); });
	connect(youtubeStreaming, &QCheckBox::toggled, this, [](bool on) {
		ConfigStore::instance().updateSection("youtube", {{"chatTransport", on ? "stream" : "poll"}});
	});
	connect(youtubeStreamCost, &QSpinBox::valueChanged, this,
		[](int units) { ConfigStore::instance().updateSection("youtube", {{"streamCost", units}}); });
	return box;
}

QWidget *SocialFeedDock::buildHistoryTab()
{
	auto *page = new QWidget();
	auto *layout = new QVBoxLayout(page);

	history = new QTreeWidget();
	history->setHeaderLabels({tr("Time"), tr("Platform"), tr("Event")});
	history->setRootIsDecorated(false);
	history->setAlternatingRowColors(true);
	history->header()->setSectionResizeMode(2, QHeaderView::Stretch);
	layout->addWidget(history, 1);

	auto *buttons = new QHBoxLayout();
	auto *replay = new QPushButton(tr("Replay"));
	auto *clear = new QPushButton(tr("Clear"));
	buttons->addWidget(replay);
	buttons->addStretch();
	buttons->addWidget(clear);
	layout->addLayout(buttons);

	auto doReplay = [this]() {
		QTreeWidgetItem *item = history->currentItem();
		if (item)
			EventBus::instance().replay(item->data(0, Qt::UserRole).toString());
	};
	connect(replay, &QPushButton::clicked, this, doReplay);
	connect(history, &QTreeWidget::itemDoubleClicked, this, doReplay);
	connect(clear, &QPushButton::clicked, this, []() { EventBus::instance().clearHistory(); });
	return page;
}

QWidget *SocialFeedDock::buildTestTab()
{
	auto *scroll = new QScrollArea();
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget();
	auto *layout = new QVBoxLayout(content);

	auto *hint = new QLabel(tr("Send sample items to every Chat Feed and Event Display. Test items ignore "
				   "channel settings and are not stored in the event history."));
	hint->setWordWrap(true);
	layout->addWidget(hint);

	testPlatform = new QComboBox();
	testPlatform->addItem(tr("Random platform"), QString());
	for (const QString &platform : allPlatforms())
		testPlatform->addItem(platformLabel(platform), platform);
	layout->addWidget(testPlatform);

	auto *chatButton = new QPushButton(tr("Chat message"));
	layout->addWidget(chatButton);
	connect(chatButton, &QPushButton::clicked, this,
		[this]() { EventBus::instance().publish(sampleChatMessage(testPlatform->currentData().toString())); });

	auto *grid = new QGridLayout();
	int index = 0;
	for (const QString &type : allEventTypes()) {
		auto *button = new QPushButton(eventTypeLabel(type));
		grid->addWidget(button, index / 2, index % 2);
		index++;
		connect(button, &QPushButton::clicked, this, [this, type]() {
			EventBus::instance().publish(sampleEvent(type, testPlatform->currentData().toString()));
		});
	}
	layout->addLayout(grid);
	layout->addStretch();
	scroll->setWidget(content);
	return scroll;
}

void SocialFeedDock::refreshStatus(const QString &providerId)
{
	QLabel *label = statusLabels.value(providerId);
	if (!label)
		return;
	ProviderStatus status = ProviderManager::instance().status(providerId);
	QString color;
	switch (status.state) {
	case ProviderStatus::State::Connected:
		color = "#3FB950";
		break;
	case ProviderStatus::State::Connecting:
		color = "#D29922";
		break;
	case ProviderStatus::State::Error:
		color = "#F85149";
		break;
	default:
		color = "#8B949E";
		break;
	}
	QString text = QStringLiteral("<span style='color:%1'>●</span> <b>%2</b>")
			       .arg(color, stateLabel(status.state).toHtmlEscaped());
	if (!status.message.isEmpty())
		text += QStringLiteral("<br>%1").arg(status.message.toHtmlEscaped());
	label->setText(text);
}

void SocialFeedDock::refreshTwitchAccount()
{
	QJsonObject section = ConfigStore::instance().section("twitch");
	QString login = section.value("login").toString();
	bool loggedIn = !section.value("accessToken").toString().isEmpty();

	twitchAccount->setText(loggedIn ? tr("Logged in as <b>%1</b>").arg(login.toHtmlEscaped())
					: tr("Not logged in. Chat from any channel still works without "
					     "logging in; alerts need a login."));
	twitchAccount->setWordWrap(true);
	twitchLogin->setVisible(!loggedIn);
	twitchLogout->setVisible(loggedIn);
	twitchEvents->setEnabled(loggedIn);
	{
		QSignalBlocker block(twitchEvents);
		twitchEvents->setChecked(section.value("eventsEnabled").toBool(true));
	}
	if (!twitchClientId->hasFocus())
		twitchClientId->setText(section.value("clientId").toString());
}

void SocialFeedDock::refreshYouTubeAccount()
{
	QJsonObject section = ConfigStore::instance().section("youtube");
	bool loggedIn = !section.value("refreshToken").toString().isEmpty();
	QString name = section.value("displayName").toString();

	youtubeAccount->setText(loggedIn ? tr("Logged in as <b>%1</b>").arg(name.toHtmlEscaped())
					 : tr("Not logged in. Chat and Super Chats appear once you are live."));
	youtubeLogin->setVisible(!loggedIn);
	youtubeLogout->setVisible(loggedIn);

	int poll = section.value("pollSeconds").toInt(youtube::kDefaultPollSeconds);
	int quota = section.value("dailyQuota").toInt(youtube::kDefaultDailyQuota);
	bool streaming = section.value("chatTransport").toString() == "stream";
	{
		QSignalBlocker blockPoll(youtubePoll);
		QSignalBlocker blockQuota(youtubeQuota);
		QSignalBlocker blockStreaming(youtubeStreaming);
		QSignalBlocker blockCost(youtubeStreamCost);
		youtubePoll->setValue(poll);
		youtubeQuota->setValue(quota);
		youtubeStreaming->setChecked(streaming);
		youtubeStreamCost->setValue(section.value("streamCost").toInt(5));
	}
	youtubePoll->setEnabled(!streaming);
	/* Budget: one liveChatMessages call per interval, minus ~1 unit/minute for broadcast
	 * checks while offline, which is small enough to ignore in the estimate. */
	double hours = (double)quota / youtube::kCostChatMessages * poll / 3600.0;
	if (streaming)
		youtubeEstimate->setText(tr("Streaming: the quota cost per connection is not documented by Google yet. "
					    "Connections are logged so it can be measured; the refresh interval is not "
					    "used while streaming."));
	else
		youtubeEstimate->setText(
			tr("≈ %1 hours of live chat per day at this refresh rate.").arg(hours, 0, 'f', 1));

	if (!youtubeClientId->hasFocus())
		youtubeClientId->setText(section.value("clientId").toString());
	if (!youtubeClientSecret->hasFocus())
		youtubeClientSecret->setText(section.value("clientSecret").toString());
}

void SocialFeedDock::refreshHistory()
{
	QString selected = history->currentItem() ? history->currentItem()->data(0, Qt::UserRole).toString()
						  : QString();
	history->clear();
	auto items = EventBus::instance().history();
	for (auto it = items.rbegin(); it != items.rend(); ++it) {
		QDateTime time = QDateTime::fromMSecsSinceEpoch((qint64)it->payload.value("timestamp").toDouble());
		auto *row = new QTreeWidgetItem(
			{time.toString("HH:mm:ss"), platformLabel(it->platform), summarizeItem(*it)});
		row->setData(0, Qt::UserRole, it->id);
		history->addTopLevelItem(row);
		if (it->id == selected)
			history->setCurrentItem(row);
	}
	history->resizeColumnToContents(0);
	history->resizeColumnToContents(1);
}

} // namespace sf::ui
