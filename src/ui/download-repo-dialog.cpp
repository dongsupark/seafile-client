#include <QtGlobal>

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include <QDirIterator>

#include <jansson.h>

#include "account-mgr.h"
#include "settings-mgr.h"
#include "utils/utils.h"
#include "seafile-applet.h"
#include "rpc/rpc-client.h"
#include "configurator.h"
#include "api/requests.h"
#include "api/api-error.h"
#include "api/server-repo.h"
#include "repo-service.h"
#include "download-repo-dialog.h"

namespace {
const int kAlternativeTryTimes = 20;
bool isPathConflictWithExistingRepo(const QString &path, QString *repo_name) {
    RepoService::instance()->refreshLocalRepoList();
    const std::vector<LocalRepo> & repos = RepoService::instance()->localRepos();
    for (unsigned i = 0; i < repos.size(); ++i) {
        // compare case insensitive file names as well
        if (QFileInfo(repos[i].worktree) == QFileInfo(path)) {
            *repo_name = repos[i].name;
            return true;
        }
    }
    return false;
}
QString getAlternativePath(const QString &dir_path, const QString &name) {
    QDir dir = QDir(dir_path);
    QFileInfo file;
    file = QFileInfo(dir.filePath(name));
    int i;
    for (i = 1; i < kAlternativeTryTimes; ++i) {
        if (!file.exists() && dir.mkdir(file.fileName()))
            return file.absoluteFilePath();
        file = QFileInfo(dir.filePath(name + " (" + QString::number(i) + ")"));
    }

    return QString();
}
} // anonymous namespace

DownloadRepoDialog::DownloadRepoDialog(const Account& account,
                                       const ServerRepo& repo,
                                       QWidget *parent)
    : QDialog(parent),
      repo_(repo),
      account_(account)
{
    auto_mode_ = !seafApplet->settingsManager()->isEnableSyncingWithExistingFolder();
    setupUi(this);
    if (!repo.isSubfolder()) {
        setWindowTitle(tr("Sync library \"%1\"").arg(repo_.name));
        mDirectory->setPlaceholderText(tr("Sync this library to:"));
    }
    else {
        setWindowTitle(tr("Sync folder \"%1\"").arg(repo.parent_path));
        mDirectory->setPlaceholderText(tr("Sync this folder to:"));
    }
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    mRepoIcon->setPixmap(repo.getPixmap());
    mRepoName->setText(repo_.name);
    mOperationText->setText(tr("Sync to folder:"));

    if (repo_.encrypted) {
        mPassword->setVisible(true);
        mPasswordLabel->setVisible(true);
    } else {
        mPassword->setVisible(false);
        mPasswordLabel->setVisible(false);
    }

    int height = 250;
#if defined(Q_OS_MAC)
    layout()->setContentsMargins(8, 9, 9, 5);
    layout()->setSpacing(6);
    verticalLayout_3->setSpacing(6);
#endif
    if (repo.encrypted) {
        height += 100;
    }
    setMinimumHeight(height);
    setMaximumHeight(height);

    setDirectoryText(seafApplet->configurator()->worktreeDir());

    if (auto_mode_) {
        mMergeHint->setText(tr("If a sub-folder with same name exists, its contents will be merged."));

        mSwitchToSyncFrame->hide();
    } else {
        sync_with_existing_ = false;
        connect(mSwitchModeHint, SIGNAL(linkActivated(const QString &)),
                this, SLOT(switchMode()));
        updateSyncMode();

        mMergeHint->hide();
    }

    connect(mChooseDirBtn, SIGNAL(clicked()), this, SLOT(chooseDirAction()));
    connect(mOkBtn, SIGNAL(clicked()), this, SLOT(onOkBtnClicked()));
}

void DownloadRepoDialog::switchMode()
{
    sync_with_existing_ = !sync_with_existing_;

    updateSyncMode();
}

void DownloadRepoDialog::updateSyncMode()
{
    QString switch_hint_text;
    QString op_text;
    const QString link_template = "<a style=\"color:#FF9A2A\" href=\"#\">%1</a>";

    QString OR = tr("or");
    if (!sync_with_existing_) {
        QString link = link_template.arg(tr("sync with an existing folder"));
        switch_hint_text = QString("%1 %2").arg(OR).arg(link);

        op_text = tr("Create a new sync folder at:");
    } else {
        QString link = link_template.arg(tr("create a new sync folder"));
        switch_hint_text = QString("%1 %2").arg(OR).arg(link);

        op_text = tr("Sync with this existing folder:");

        if (!alternative_path_.isNull()) {
            setDirectoryText(alternative_path_);
        }
    }

    mOperationText->setText(op_text);
    mSwitchModeHint->setText(switch_hint_text);
}

void DownloadRepoDialog::setDirectoryText(const QString& path)
{
    QString text = path;
    if (text.endsWith("/")) {
        text.resize(text.size() - 1);
    }

    mDirectory->setText(text);

    if (!auto_mode_ && sync_with_existing_) {
        alternative_path_ = text;
    }
}

void DownloadRepoDialog::chooseDirAction()
{
    const QString &wt = seafApplet->configurator()->worktreeDir();
    QString dir = QFileDialog::getExistingDirectory(this, tr("Please choose a folder"),
                                                    wt.toUtf8().data(),
                                                    QFileDialog::ShowDirsOnly
                                                    | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty())
        return;
    setDirectoryText(dir);
}

void DownloadRepoDialog::onOkBtnClicked()
{
    if (!validateInputs()) {
        return;
    }

    setAllInputsEnabled(false);

    DownloadRepoRequest *req = new DownloadRepoRequest(account_, repo_.id, repo_.readonly);
    connect(req, SIGNAL(success(const RepoDownloadInfo&)),
            this, SLOT(onDownloadRepoRequestSuccess(const RepoDownloadInfo&)));
    connect(req, SIGNAL(failed(const ApiError&)),
            this, SLOT(onDownloadRepoRequestFailed(const ApiError&)));
    req->send();
}

bool DownloadRepoDialog::validateInputs()
{
    if (auto_mode_) {
        return validateInputsAutoMode();
    }

    setDirectoryText(mDirectory->text().trimmed());
    if (mDirectory->text().isEmpty()) {
        QMessageBox::warning(this, getBrand(),
                             tr("Please choose the folder to sync"),
                             QMessageBox::Ok);
        return false;
    }
    QDir dir(mDirectory->text());
    if (!dir.exists()) {
        QMessageBox::warning(this, getBrand(),
                             tr("The folder does not exist"),
                             QMessageBox::Ok);
        return false;
    }
    if (repo_.encrypted) {
        mPassword->setText(mPassword->text().trimmed());
        if (mPassword->text().isEmpty()) {
            QMessageBox::warning(this, getBrand(),
                                 tr("Please enter the password"),
                                 QMessageBox::Ok);
            return false;
        }
    }
    return true;
}

bool DownloadRepoDialog::validateInputsAutoMode()
{
    setDirectoryText(mDirectory->text().trimmed());
    if (mDirectory->text().isEmpty()) {
        QMessageBox::warning(this, getBrand(),
                             tr("Please choose the folder to sync."),
                             QMessageBox::Ok);
        return false;
    }
    sync_with_existing_ = false;
    alternative_path_ = QString();
    QString path = QDir(mDirectory->text()).absoluteFilePath(repo_.name);
    QFileInfo fileinfo = QFileInfo(path);
    if (fileinfo.exists()) {
        // exist and but not a directory ?
        if (!fileinfo.isDir()) {
            QMessageBox::warning(this, getBrand(),
                                 tr("Conflicting with existing file \"%1\", please choose a different folder.").arg(path),
                                 QMessageBox::Ok);
            return false;
        }
        // exist and but conflicting?
        QString repo_name;
        if (isPathConflictWithExistingRepo(path, &repo_name)) {
            QMessageBox::warning(this, getBrand(),
                                 tr("Conflicting with existing library \"%1\", please choose a different folder.").arg(repo_name),
                                 QMessageBox::Ok);
            return false;
        }
        sync_with_existing_ = true;
        int ret = QMessageBox::question(
            this, getBrand(), tr("Are you sure to sync with the existing folder \"%1\"?")
                                  .arg(path) + QString("<br/><small>%1</small>").arg(tr("Click No to sync with a new folder instead")),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (ret & QMessageBox::Cancel)
            return false;
        if (ret & QMessageBox::No) {
            QString new_path = getAlternativePath(mDirectory->text(), repo_.name);
            if (new_path.isEmpty()) {
                QMessageBox::warning(this, getBrand(),
                                     tr("Unable to find a alternative folder name").arg(path),
                                     QMessageBox::Ok);
                return false;
            }
            alternative_path_ = new_path;
        }
    }
    if (repo_.encrypted) {
        mPassword->setText(mPassword->text().trimmed());
        if (mPassword->text().isEmpty()) {
            QMessageBox::warning(this, getBrand(),
                                 tr("Please enter the password"),
                                 QMessageBox::Ok);
            return false;
        }
    }
    return true;
}

void DownloadRepoDialog::setAllInputsEnabled(bool enabled)
{
    mDirectory->setEnabled(enabled);
    mChooseDirBtn->setEnabled(enabled);
    mPassword->setEnabled(enabled);
    mOkBtn->setEnabled(enabled);
}

void DownloadRepoDialog::onDownloadRepoRequestSuccess(const RepoDownloadInfo& info)
{
    QString worktree = mDirectory->text();
    QString password = repo_.encrypted ? mPassword->text() : QString();
    int ret = 0;
    QString error;

    if (sync_with_existing_) {
        if (alternative_path_.isEmpty())
            worktree = QDir(worktree).absoluteFilePath(repo_.name);
        else
            worktree = alternative_path_;
        fprintf(stderr, "merging with %s\n", worktree.toUtf8().data());
        ret = seafApplet->rpcClient()->cloneRepo(info.repo_id, info.repo_version,
                                                 info.relay_id,
                                                 repo_.name, worktree,
                                                 info.token, password,
                                                 info.magic, info.relay_addr,
                                                 info.relay_port, info.email,
                                                 info.random_key, info.enc_version,
                                                 info.more_info,
                                                 &error);
    } else {
        fprintf(stderr, "download to %s\n", worktree.toUtf8().data());
        ret = seafApplet->rpcClient()->downloadRepo(info.repo_id, info.repo_version,
                                                    info.relay_id,
                                                    repo_.name, worktree,
                                                    info.token, password,
                                                    info.magic, info.relay_addr,
                                                    info.relay_port, info.email,
                                                    info.random_key, info.enc_version,
                                                    info.more_info,
                                                    &error);
    }

    if (ret < 0) {
        QMessageBox::warning(this, getBrand(),
                             tr("Failed to add download task:\n %1").arg(error),
                             QMessageBox::Ok);
        setAllInputsEnabled(true);
    } else {
        done(QDialog::Accepted);
    }
}


void DownloadRepoDialog::onDownloadRepoRequestFailed(const ApiError& error)
{
    QString msg = tr("Failed to get repo download information:\n%1").arg(error.toString());

    seafApplet->warningBox(msg, this);

    setAllInputsEnabled(true);
}

void DownloadRepoDialog::setMergeWithExisting(const QString& localPath) {
    setDirectoryText(localPath);
}
