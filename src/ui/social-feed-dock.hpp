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

#include <QDialog>
#include <QHash>
#include <QWidget>

#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace sf::ui {

/* Twitch device-code login: shows the code, opens twitch.tv/activate and polls for the token
 * on a worker thread. */
class TwitchLoginDialog : public QDialog {
	Q_OBJECT

public:
	explicit TwitchLoginDialog(QWidget *parent = nullptr);
	~TwitchLoginDialog() override;

private:
	void start();
	void showCode(const QString &userCode, const QString &uri);
	void fail(const QString &message);

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
	QWidget *buildHistoryTab();
	QWidget *buildTestTab();
	void refreshStatus(const QString &providerId);
	void refreshTwitchAccount();
	void refreshHistory();

	QHash<QString, QLabel *> statusLabels;
	QLabel *twitchAccount = nullptr;
	QPushButton *twitchLogin = nullptr;
	QPushButton *twitchLogout = nullptr;
	QCheckBox *twitchEvents = nullptr;
	QLineEdit *twitchClientId = nullptr;
	QTreeWidget *history = nullptr;
	QComboBox *testPlatform = nullptr;
};

} // namespace sf::ui
