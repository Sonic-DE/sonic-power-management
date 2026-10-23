// SPDX-License-Identifier: LGPL-2.0-only

#pragma once

#include "displaybrightness.h"

#include <QDBusServiceWatcher>
#include <QMap>
#include <QVariantMap>
#include <optional>

// These constants form the optional X11 session-bus protocol with SonicWin.
namespace SoftwareBrightnessDBus {
inline constexpr auto service = "org.kde.KWin";
inline constexpr auto path = "/org/sonicde/SoftwareBrightness";
inline constexpr auto interface = "org.sonicde.SoftwareBrightness";
}

class SoftwareBrightness : public DisplayBrightness {
    Q_OBJECT
public:
    SoftwareBrightness(const QString &name, const QVariantMap &info, QObject *parent);
    QString id() const override;
    QString label() const override;
    int knownSafeMinBrightness() const override;
    int maxBrightness() const override;
    int brightness() const override;
    void setBrightness(int value, bool allowAnimations = true) override;
    bool isInternal() const override;
    std::optional<QByteArray> edidData() const override;
    bool supportsDimmingMultiplier() const override;
    void setDimmingMultiplier(double multiplier) override;
    void observeBrightness(int value);
    bool matchesInfo(const QVariantMap &info) const;

private:
    void send();
    QString m_name;
    QString m_label;
    QByteArray m_edid;
    bool m_internal;
    int m_brightness;
    int m_confirmedBrightness;
    double m_dimming = 1.0;
    double m_confirmedDimming = 1.0;
    quint64 m_request = 0;
    bool m_pending = false;
};

// Ignore the different hardware/software value ranges when matching devices.
// A working hardware control should get the only slider for that physical display.
bool needsSoftwareBrightness(const DisplayBrightness *display, const QList<DisplayBrightness *> &hardware);

class SoftwareBrightnessDetector : public DisplayBrightnessDetector {
    Q_OBJECT
public:
    explicit SoftwareBrightnessDetector(QObject *parent);
    void detect() override;
    QList<DisplayBrightness *> displays() const override;

private Q_SLOTS:
    void refresh();
    void observeBrightness(const QString &name, int value);

private:
    void clear();
    QDBusServiceWatcher m_watcher;
    QMap<QString, SoftwareBrightness *> m_displays;
    quint64 m_generation = 0;
    bool m_detecting = false;
};
