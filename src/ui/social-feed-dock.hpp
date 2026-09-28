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

#pragma once

#include "providers/oauth-device.hpp"

#include <QDialog>
#include <QHash>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;

namespace sf::ui {

/* One OAuth device-code login. requestCode/poll/finish run on a worker thread; finish stores
 * the login and returns an error message, or an empty string on success. */
struct DeviceFlow {
	QString platform;          /* shown in texts, e.g. "Twitch" */
	QString missingSetupError; /* non-empty: show this instead of starting */
	std::function<oauth::DeviceCode()> requestCode;
	std::function<oauth::TokenResult(const QString &deviceCode)> poll;
	std::function<QString(const oauth::TokenResult &token)> finish;
};

/* Shows the user code, opens the verification page and polls for the token. */
class DeviceLoginDialog : public QDialog {
	Q_OBJECT

public:
	DeviceLoginDialog(DeviceFlow flow, QWidget *parent = nullptr);
	~DeviceLoginDialog() override;

private:
	void start();
	void showCode(const QString &userCode, const QString &uri);
	void fail(const QString &message);

	DeviceFlow flow;
	QLabel *codeLabel;
	QLabel *statusLabel;
	QPushButton *openButton;
	QString verificationUri;
	std::shared_ptr<std::atomic<bool>> cancelled;
};

/* "Social Feed" dock: accounts/connection status, event history with replay, and test
 * buttons that fire sample chat messages and events into every source. */
class SocialFeedDock : public QWidget {
	Q_OBJECT

public:
	explicit SocialFeedDock(QWidget *parent = nullptr);

private:
	QWidget *buildAccountsTab();
	QWidget *buildTwitchBox();
	QWidget *buildYouTubeBox();
	QWidget *buildHistoryTab();
	QWidget *buildTestTab();
	void refreshStatus(const QString &providerId);
	void refreshTwitchAccount();
	void refreshYouTubeAccount();
	void refreshHistory();

	QHash<QString, QLabel *> statusLabels;
	QLabel *twitchAccount = nullptr;
	QPushButton *twitchLogin = nullptr;
	QPushButton *twitchLogout = nullptr;
	QCheckBox *twitchEvents = nullptr;
	QLineEdit *twitchClientId = nullptr;
	QLabel *youtubeAccount = nullptr;
	QPushButton *youtubeLogin = nullptr;
	QPushButton *youtubeLogout = nullptr;
	QLineEdit *youtubeClientId = nullptr;
	QLineEdit *youtubeClientSecret = nullptr;
	QSpinBox *youtubePoll = nullptr;
	QSpinBox *youtubeQuota = nullptr;
	QLabel *youtubeEstimate = nullptr;
	QCheckBox *youtubeStreaming = nullptr;
	QSpinBox *youtubeStreamCost = nullptr;
	QTreeWidget *history = nullptr;
	QComboBox *testPlatform = nullptr;
};

} // namespace sf::ui
