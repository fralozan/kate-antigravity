#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <KLocalizedString>
#include "projectdetector.h"

class TestProjectDetector : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testCurrentRepoDetection()
    {
        // Detect project from this source file
        const QString testFilePath = QStringLiteral(CMAKE_CURRENT_SOURCE_DIR "/src/chatwidget.cpp");
        ProjectInfo info = ProjectDetector::detectProject(testFilePath);

        QVERIFY(info.isValid());
        QVERIFY(info.isGit);
        QCOMPARE(info.name, QStringLiteral("kate-agy"));
        QVERIFY(!info.gitBranch.isEmpty());
        QVERIFY(info.displayName().contains(QStringLiteral("kate-agy")));
        QVERIFY(info.displayName().contains(info.gitBranch));
    }

    void testMarkerDetectionWithoutGit()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Create a subfolder structure: tempDir/modules/my_module/file.php
        QDir rootDir(tempDir.path());
        rootDir.mkdir(QStringLiteral("modules"));
        rootDir.mkdir(QStringLiteral("modules/my_module"));

        // Create composer.json in root
        QFile composerFile(rootDir.filePath(QStringLiteral("composer.json")));
        QVERIFY(composerFile.open(QIODevice::WriteOnly));
        composerFile.write("{}");
        composerFile.close();

        // Target file in subfolder
        const QString fileInModule = rootDir.filePath(QStringLiteral("modules/my_module/file.php"));
        ProjectInfo info = ProjectDetector::detectProject(fileInModule);

        QVERIFY(info.isValid());
        QCOMPARE(info.isGit, false);
        QCOMPARE(info.rootPath, QDir(tempDir.path()).canonicalPath());
        QCOMPARE(info.name, QDir(tempDir.path()).dirName());
    }

    void testEmptyPathGlobalFallback()
    {
        ProjectInfo info = ProjectDetector::detectProject(QString());
        QVERIFY(info.isGlobal());
        QVERIFY(!info.isValid());
        QCOMPARE(info.displayName(), i18n("Global / General"));
    }
};

QTEST_MAIN(TestProjectDetector)
#include "test_projectdetector.moc"
