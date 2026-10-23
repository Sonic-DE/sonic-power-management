// SPDX-License-Identifier: LGPL-2.0-only

#include "softwarebrightness.h"
#include <powerdevil_debug.h>

#include <KLocalizedString>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <algorithm>
#include <cmath>

namespace {
QDBusMessage method(const char *name)
{
    return QDBusMessage::createMethodCall(QString::fromLatin1(SoftwareBrightnessDBus::service),
                                          QString::fromLatin1(SoftwareBrightnessDBus::path),
                                          QString::fromLatin1(SoftwareBrightnessDBus::interface),
                                          QString::fromLatin1(name));
}
}

SoftwareBrightness::SoftwareBrightness(const QString &name, const QVariantMap &info, QObject *parent)
    : DisplayBrightness(parent)
    , m_name(name)
    , m_label(info.value(QStringLiteral("label")).toString())
    , m_edid(info.value(QStringLiteral("edid")).toByteArray())
    , m_internal(info.value(QStringLiteral("internal")).toBool())
    , m_brightness(std::clamp(info.value(QStringLiteral("brightness"), 10000).toInt(), 100, 10000))
    , m_confirmedBrightness(m_brightness)
{
    // A previous PowerDevil instance may have exited while idle-dimmed.
    // Keep the persisted user brightness, but reset its temporary multiplier.
    send();
}

QString SoftwareBrightness::id() const
{
    return m_name;
}
QString SoftwareBrightness::label() const
{
    return i18nc("Display brightness slider label", "%1 (software dimming)", m_label.isEmpty() ? m_name : m_label);
}
int SoftwareBrightness::knownSafeMinBrightness() const
{
    return 100;
}
int SoftwareBrightness::maxBrightness() const
{
    return 10000;
}
int SoftwareBrightness::brightness() const
{
    return m_brightness;
}
bool SoftwareBrightness::isInternal() const
{
    return m_internal;
}
std::optional<QByteArray> SoftwareBrightness::edidData() const
{
    return m_edid.isEmpty() ? std::nullopt : std::make_optional(m_edid);
}
bool SoftwareBrightness::supportsDimmingMultiplier() const
{
    return true;
}

void SoftwareBrightness::setBrightness(int value, bool)
{
    value = std::clamp(value, knownSafeMinBrightness(), maxBrightness());
    if (m_brightness == value) {
        return;
    }
    m_brightness = value;
    send();
}

void SoftwareBrightness::setDimmingMultiplier(double multiplier)
{
    if (!std::isfinite(multiplier)) {
        return;
    }
    multiplier = std::clamp(multiplier, 0.0, 1.0);
    if (m_dimming == multiplier) {
        return;
    }
    m_dimming = multiplier;
    send();
}

void SoftwareBrightness::send()
{
    auto message = method("SetBrightness");
    message << m_name << m_brightness << m_dimming;
    const quint64 request = ++m_request;
    const int value = m_brightness;
    const double dimming = m_dimming;
    m_pending = true;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request, value, dimming] {
        const QDBusPendingReply<bool> reply = *watcher;
        watcher->deleteLater();
        if (request != m_request) {
            return;
        }
        m_pending = false;
        if (reply.isError() || !reply.value()) {
            qCWarning(POWERDEVIL) << "Failed to set software brightness for" << m_name << reply.error();
            m_brightness = m_confirmedBrightness;
            m_dimming = m_confirmedDimming;
            Q_EMIT externalBrightnessChangeObserved(this, m_brightness);
        } else {
            m_confirmedBrightness = value;
            m_confirmedDimming = dimming;
        }
    });
}

void SoftwareBrightness::observeBrightness(int value)
{
    value = std::clamp(value, knownSafeMinBrightness(), maxBrightness());
    m_confirmedBrightness = value;
    if (!m_pending && m_brightness != value) {
        m_brightness = value;
        Q_EMIT externalBrightnessChangeObserved(this, value);
    }
}

bool SoftwareBrightness::matchesInfo(const QVariantMap &info) const
{
    return m_label == info.value(QStringLiteral("label")).toString() && m_edid == info.value(QStringLiteral("edid")).toByteArray()
        && m_internal == info.value(QStringLiteral("internal")).toBool();
}

bool needsSoftwareBrightness(const DisplayBrightness *display, const QList<DisplayBrightness *> &hardware)
{
    const auto edid = display->edidData();
    for (const auto *candidate : hardware) {
        if (display->isInternal() && candidate->isInternal()) {
            return false;
        }
        const auto hardwareEdid = candidate->edidData();
        if (edid && hardwareEdid && !edid->isEmpty() && !hardwareEdid->isEmpty() && (edid->startsWith(*hardwareEdid) || hardwareEdid->startsWith(*edid))) {
            return false;
        }
    }
    return true;
}

SoftwareBrightnessDetector::SoftwareBrightnessDetector(QObject *parent)
    : DisplayBrightnessDetector(parent)
    , m_watcher(QString::fromLatin1(SoftwareBrightnessDBus::service), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange, this)
{
    auto bus = QDBusConnection::sessionBus();
    bus.connect(QString::fromLatin1(SoftwareBrightnessDBus::service),
                QString::fromLatin1(SoftwareBrightnessDBus::path),
                QString::fromLatin1(SoftwareBrightnessDBus::interface),
                QStringLiteral("DisplaysChanged"),
                this,
                SLOT(refresh()));
    bus.connect(QString::fromLatin1(SoftwareBrightnessDBus::service),
                QString::fromLatin1(SoftwareBrightnessDBus::path),
                QString::fromLatin1(SoftwareBrightnessDBus::interface),
                QStringLiteral("BrightnessChanged"),
                this,
                SLOT(observeBrightness(QString, int)));
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] {
        ++m_generation; // discard replies from a previous WM instance
        clear();
        refresh();
    });
}

void SoftwareBrightnessDetector::detect()
{
    m_detecting = true;
    refresh();
}

QList<DisplayBrightness *> SoftwareBrightnessDetector::displays() const
{
    QList<DisplayBrightness *> result;
    for (auto display : m_displays) {
        result.append(display);
    }
    return result;
}

void SoftwareBrightnessDetector::clear()
{
    if (m_displays.isEmpty()) {
        return;
    }
    qDeleteAll(m_displays);
    m_displays.clear();
    Q_EMIT displaysChanged();
}

void SoftwareBrightnessDetector::refresh()
{
    const quint64 generation = ++m_generation;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(method("Displays")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation] {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        if (generation != m_generation) {
            return;
        }
        const QVariantMap infos = reply.isError() ? QVariantMap{} : reply.value();
        bool changed = false;
        for (auto it = m_displays.begin(); it != m_displays.end();) {
            const QVariantMap info = qdbus_cast<QVariantMap>(infos.value(it.key()));
            if (!infos.contains(it.key()) || !it.value()->matchesInfo(info)) {
                delete it.value();
                it = m_displays.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        for (auto it = infos.begin(); it != infos.end(); ++it) {
            const auto info = qdbus_cast<QVariantMap>(it.value());
            if (!m_displays.contains(it.key())) {
                m_displays.insert(it.key(), new SoftwareBrightness(it.key(), info, this));
                changed = true;
            } else {
                m_displays.value(it.key())->observeBrightness(info.value(QStringLiteral("brightness")).toInt());
            }
        }
        if (changed) {
            Q_EMIT displaysChanged();
        }
        if (m_detecting) {
            m_detecting = false;
            Q_EMIT detectionFinished(!m_displays.isEmpty());
        }
    });
}

void SoftwareBrightnessDetector::observeBrightness(const QString &name, int value)
{
    if (auto display = m_displays.value(name)) {
        display->observeBrightness(value);
    }
}
