/*
    Copyright (c) 2018 Daniel Vrátil <dvratil@kde.org>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#include "agentconfigurationwidget.h"
#include "core/agentconfigurationmanager_p.h"
#include "core/agentconfigurationbase.h"
#include "core/agentconfigurationfactorybase.h"
#include "core/agentmanager.h"
#include "core/servermanager.h"
#include "akonadiwidgets_debug.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QPluginLoader>
#include <QJsonObject>
#include <QJsonValue>
#include <QStackedLayout>
#include <QLabel>

#include <KSharedConfig>
#include <KLocalizedString>

#include <memory>

namespace Akonadi {

namespace {

struct PluginLoaderDeleter
{
    void operator()(QPluginLoader *loader) {
        loader->unload();
        delete loader;
    }
};

}

class Q_DECL_HIDDEN AgentConfigurationWidget::Private {
public:
    Private(const AgentInstance &instance)
        : agentInstance(instance)
    {
    }

    void setupErrorWidget(QWidget *parent, const QString &text)
    {
        auto layout = new QVBoxLayout;
        layout->addStretch(2);
        auto label = new QLabel(text, parent);
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        layout->addStretch(2);

        parent->setLayout(layout);
    }

    bool loadPlugin(const QString &pluginPath)
    {
        loader = decltype(loader)(new QPluginLoader(pluginPath));
        if (!loader->load()) {
            qCWarning(AKONADIWIDGETS_LOG) << "Failed to load config plugin" << pluginPath << ":" << loader->errorString();
            loader.reset();
            return false;
        }
        factory = qobject_cast<AgentConfigurationFactoryBase*>(loader->instance());
        if (!factory) {
            // will unload the QPluginLoader and thus delete the factory as well
            qCWarning(AKONADIWIDGETS_LOG) << "Config plugin" << pluginPath << "does not contain AgentConfigurationFactory!";
            loader.reset();
            return false;
        }

        return true;
    }

    std::unique_ptr<QPluginLoader, PluginLoaderDeleter> loader;
    QPointer<AgentConfigurationFactoryBase> factory = nullptr;
    QPointer<AgentConfigurationBase> plugin = nullptr;
    AgentInstance agentInstance;
};

}

using namespace Akonadi;

AgentConfigurationWidget::AgentConfigurationWidget(const Akonadi::AgentInstance &instance, QWidget *parent)
    : QWidget(parent)
    , d(new Private(instance))
{
    if (AgentConfigurationManager::self()->registerInstanceConfiguration(instance.identifier())) {
        const auto pluginPath = AgentConfigurationManager::self()->findConfigPlugin(instance.type().identifier());
        if (d->loadPlugin(pluginPath)) {
            const auto configPath = ServerManager::self()->agentConfigFilePath(instance.identifier());
            KSharedConfigPtr config = KSharedConfig::openConfig(configPath);
            d->plugin = d->factory->create(config, this, { instance.identifier() });
        } else {
            d->setupErrorWidget(this, i18n("Failed to load configuration plugin"));
        }
    } else if (AgentConfigurationManager::self()->isInstanceRegistered(instance.identifier())) {
        d->setupErrorWidget(this, i18n("Configuration for this %1 is already opened elsewhere.", instance.name()));
    } else {
        d->setupErrorWidget(this, i18n("Failed to register %1 configuration dialog.", instance.name()));
    }
}

AgentConfigurationWidget::~AgentConfigurationWidget()
{
    AgentConfigurationManager::self()->unregisterInstanceConfiguration(d->agentInstance.identifier());
}

void AgentConfigurationWidget::load()
{
    if (d->plugin) {
        d->plugin->load();
    }
}

void AgentConfigurationWidget::save()
{
    if (d->plugin) {
        d->plugin->save();
    }
}

