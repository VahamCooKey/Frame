#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class PlayerController;
class SettingsStore;

// One owner for translating saved preferences into live mpv options. It
// never selects aid/sid: session TrackPreference and explicit Off win over
// the saved language defaults that mpv uses for automatic file selection.
class PlaybackPreferences : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString defaultAspect READ defaultAspect NOTIFY fileDefaultsChanged)
    Q_PROPERTY(int defaultAspectIndex READ defaultAspectIndex NOTIFY fileDefaultsChanged)
    Q_PROPERTY(QVariantMap fileDefaults READ fileDefaults NOTIFY fileDefaultsChanged)
    Q_PROPERTY(QVariantList languageOptions READ languageOptions CONSTANT)
    Q_PROPERTY(QString preferredAudioLanguage READ preferredAudioLanguage WRITE setPreferredAudioLanguage NOTIFY languagePreferencesChanged)
    Q_PROPERTY(QString preferredSubtitleLanguage READ preferredSubtitleLanguage WRITE setPreferredSubtitleLanguage NOTIFY languagePreferencesChanged)
    Q_PROPERTY(QStringList shaders READ shaders NOTIFY shadersChanged)
    Q_PROPERTY(bool shadersEnabled READ shadersEnabled WRITE setShadersEnabled NOTIFY shadersChanged)
    // A54. Where screenshots go while general/screenshotDirectory is empty.
    // mpv's own fallback is the process working directory, which for a
    // file opened from Explorer, the Start menu or a second launch is three
    // different folders; Pictures/Frame is one place the user can find.
    Q_PROPERTY(QString defaultScreenshotDirectory READ defaultScreenshotDirectory CONSTANT)

public:
    // Construct after PlayerController initializes and before applying any
    // saved options or loading media. Captures the initialized backend cache
    // default (including diagnostic overrides), then applies all preferences.
    explicit PlaybackPreferences(SettingsStore *settings, PlayerController *player,
                                 QObject *parent = nullptr);

    QString defaultAspect() const;
    int defaultAspectIndex() const;
    // Native separators; mpv echoes this prefix back in its screenshot log
    // line, so the toast shows the same spelling Explorer does.
    static QString defaultScreenshotDirectoryPath();
    QString defaultScreenshotDirectory() const { return defaultScreenshotDirectoryPath(); }
    // Parent copies this map to PlayerController. Apply file-local-options/*
    // synchronously in its on_load hook, before playback/presentation starts.
    // Keyboard/menu corrections then last for the current file only.
    QVariantMap fileDefaults() const;
    QVariantList languageOptions() const;
    QString preferredAudioLanguage() const;
    QString preferredSubtitleLanguage() const;
    Q_INVOKABLE void setPreferredAudioLanguage(const QString &language);
    Q_INVOKABLE void setPreferredSubtitleLanguage(const QString &language);

    QStringList shaders() const;
    bool shadersEnabled() const;
    Q_INVOKABLE void setShadersEnabled(bool enabled);
    Q_INVOKABLE void removeShader(const QString &path);
    Q_INVOKABLE void resetShaders();
    Q_INVOKABLE void applyAll();

signals:
    void fileDefaultsChanged();
    void languagePreferencesChanged();
    void shadersChanged();

private:
    void applySetting(const QString &key);
    void applyShaders();
    void setOption(const QString &name, const QVariant &value);

    SettingsStore *m_settings;
    PlayerController *m_player;
    QVariant m_initialCacheBytes;
    QVariant m_initialHwdec;
};
