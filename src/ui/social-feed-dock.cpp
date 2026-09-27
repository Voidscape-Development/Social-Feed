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

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QThread>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <chrono>
#include <thread>

namespace sf::ui {

/* ---- TwitchLoginDialog ---- */

TwitchLoginDialog::TwitchLoginDialog(QWidget *parent)
	: QDialog(parent),
	  cancelled(std::make_shared<std::atomic<bool>>(false))
{
	setWindowTitle(tr("Log in with Twitch"));
	setMinimumWidth(420);

	auto *layout = new QVBoxLayout(this);
	auto *intro = new QLabel(tr("Open the Twitch activation page and enter this code:"));
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

	openButton = new QPushButton(tr("Open twitch.tv/activate"));
	openButton->setEnabled(false);
	layout->addWidget(openButton);
	connect(openButton, &QPushButton::clicked, this,
		[this]() { QDesktopServices::openUrl(QUrl(verificationUri)); });

	statusLabel = new QLabel(tr("Requesting a code from Twitch…"));
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	start();
}

TwitchLoginDialog::~TwitchLoginDialog()
{
	*cancelled = true;
}

void TwitchLoginDialog::showCode(const QString &userCode, const QString &uri)
{
	codeLabel->setText(userCode);
	verificationUri = uri;
	openButton->setEnabled(true);
	statusLabel->setText(tr("Waiting for you to approve the login on Twitch…"));
	QDesktopServices::openUrl(QUrl(uri));
}

void TwitchLoginDialog::fail(const QString &message)
{
	statusLabel->setText(message);
	openButton->setEnabled(false);
}

void TwitchLoginDialog::start()
{
	QString clientId = twitch::clientId();
	if (clientId.isEmpty()) {
		fail(tr("No Twitch Client ID is configured. Register an application at dev.twitch.tv (category "
			"\"Broadcaster Suite\", client type \"Public\") and enter its Client ID in the Social Feed "
			"dock's advanced settings."));
		return;
	}

	QPointer<TwitchLoginDialog> self(this);
	auto cancel = cancelled;
	std::thread([self, cancel, clientId]() {
		auto post = [&](auto fn) {
			QMetaObject::invokeMethod(
				qApp,
				[self, fn]() {
					if (self)
						fn(self.data());
				},
				Qt::QueuedConnection);
		};

		twitch::DeviceCode code = twitch::requestDeviceCode(clientId);
		if (!code.ok) {
			post([code](TwitchLoginDialog *d) {
				d->fail(tr("Twitch refused the login request: %1").arg(code.error));
			});
			return;
		}
		post([code](TwitchLoginDialog *d) { d->showCode(code.userCode, code.verificationUri); });

		int interval = code.intervalSeconds;
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(code.expiresInSeconds);
		while (!*cancel && std::chrono::steady_clock::now() < deadline) {
			for (int i = 0; i < interval * 10 && !*cancel; i++)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (*cancel)
				return;

			twitch::TokenResult token = twitch::pollDeviceToken(clientId, code.deviceCode);
			switch (token.status) {
			case twitch::TokenResult::Status::Pending:
				continue;
			case twitch::TokenResult::Status::SlowDown:
				interval += 5;
				continue;
			case twitch::TokenResult::Status::Success: {
				twitch::ValidateResult identity = twitch::validateToken(token.accessToken);
				if (*cancel)
					return;
				twitch::storeLogin(token, identity);
				post([](TwitchLoginDialog *d) { d->accept(); });
				return;
			}
			case twitch::TokenResult::Status::Denied:
				post([](TwitchLoginDialog *d) { d->fail(tr("The login was declined on Twitch.")); });
				return;
			case twitch::TokenResult::Status::Expired:
				post([](TwitchLoginDialog *d) {
					d->fail(tr("The code expired. Close this window and try again."));
				});
				return;
			case twitch::TokenResult::Status::Error:
				post([token](TwitchLoginDialog *d) {
					d->fail(tr("Login failed: %1").arg(token.error));
				});
				return;
			}
		}
		post([](TwitchLoginDialog *d) { d->fail(tr("The code expired. Close this window and try again.")); });
	}).detach();
}

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
	});
	refreshTwitchAccount();

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

	/* Twitch */
	auto *twitchBox = new QGroupBox(tr("Twitch"));
	auto *grid = new QGridLayout(twitchBox);
	twitchAccount = new QLabel();
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

	auto *advanced = new QGroupBox(tr("Advanced"));
	advanced->setCheckable(true);
	advanced->setChecked(false);
	auto *advLayout = new QVBoxLayout(advanced);
	auto *advWidget = new QWidget();
	auto *advInner = new QVBoxLayout(advWidget);
	advInner->setContentsMargins(0, 0, 0, 0);
	twitchClientId = new QLineEdit();
	twitchClientId->setPlaceholderText(QString::fromUtf8(SOCIAL_FEED_TWITCH_CLIENT_ID).isEmpty()
						   ? tr("Twitch application Client ID")
						   : tr("Built-in Client ID"));
	advInner->addWidget(new QLabel(tr("Client ID override:")));
	advInner->addWidget(twitchClientId);
	advLayout->addWidget(advWidget);
	advWidget->setVisible(false);
	connect(advanced, &QGroupBox::toggled, advWidget, &QWidget::setVisible);
	grid->addWidget(advanced, 4, 0, 1, 2);
	layout->addWidget(twitchBox);

	connect(twitchLogin, &QPushButton::clicked, this, [this]() {
		TwitchLoginDialog dialog(this);
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

	/* Everything else, not implemented yet */
	for (const auto &provider : ProviderManager::instance().providers()) {
		if (provider->id() == "twitch")
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
