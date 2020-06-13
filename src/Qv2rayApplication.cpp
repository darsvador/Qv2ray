#include "Qv2rayApplication.hpp"

#include "base/Qv2rayBase.hpp"
#include "common/QvHelpers.hpp"
#include "core/settings/SettingsBackend.hpp"

namespace Qv2ray
{
    Qv2rayApplication::Qv2rayApplication(int &argc, char *argv[])
        : SingleApplication(argc, argv, true, User | ExcludeAppPath | ExcludeAppVersion)
    {
        LOG(MODULE_INIT, "Qv2ray Start Time: " + QSTRN(QTime::currentTime().msecsSinceStartOfDay()))
        LOG(MODULE_INIT, "Qv2ray " QV2RAY_VERSION_STRING " on " + QSysInfo::prettyProductName() + " " + QSysInfo::currentCpuArchitecture())
        DEBUG("QV2RAY_BUILD_INFO", QV2RAY_BUILD_INFO)
        DEBUG("QV2RAY_BUILD_EXTRA_INFO", QV2RAY_BUILD_EXTRA_INFO)
        DEBUG("QV2RAY_BUILD_NUMBER", QSTRN(QV2RAY_VERSION_BUILD))
    }

    bool Qv2rayApplication::SetupQv2ray()
    {
        connect(this, &SingleApplication::receivedMessage, this, &Qv2rayApplication::onMessageReceived);
        if (isSecondary())
        {
            sendMessage(JsonToString(Qv2rayProcessArgument.toJson()).toUtf8());
            return true;
        }
        return false;
    }

    void Qv2rayApplication::onMessageReceived(quint32 clientId, QByteArray msg)
    {
        LOG(MODULE_INIT, "Client ID: " + QSTRN(clientId) + " message received.")
        const auto args = Qv2rayProcessArguments::fromJson(JsonFromString(msg));
    }

    bool Qv2rayApplication::InitilizeConfigurations()
    {
        if (initilized)
        {
            LOG(MODULE_INIT, "Qv2ray has already been initilized!")
            return false;
        }
        LOG(MODULE_INIT, "Application exec path: " + applicationDirPath())
        // Non-standard paths needs special handing for "_debug"
        const auto currentPathConfig = applicationDirPath() + "/config" QV2RAY_CONFIG_DIR_SUFFIX;
        const auto homeQv2ray = QDir::homePath() + "/.qv2ray" QV2RAY_CONFIG_DIR_SUFFIX;
        // Standard paths already handles the "_debug" suffix for us.
        const auto configQv2ray = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        //
        //
        // Some built-in search paths for Qv2ray to find configs. (load the first one if possible).
        //
        QStringList configFilePaths;
        configFilePaths << currentPathConfig;
        configFilePaths << configQv2ray;
        configFilePaths << homeQv2ray;
        //
        QString configPath = "";
        bool hasExistingConfig = false;

        for (const auto &path : configFilePaths)
        {
            // Verify the config path, check if the config file exists and in the
            // correct JSON format. True means we check for config existence as
            // well. ----------------------------------------------|HERE|
            bool isValidConfigPath = CheckSettingsPathAvailability(path, true);

            // If we already found a valid config file. just simply load it...
            if (hasExistingConfig)
                break;

            if (isValidConfigPath)
            {
                DEBUG(MODULE_INIT, "Path: " + path + " is valid.")
                configPath = path;
                hasExistingConfig = true;
            }
            else
            {
                LOG(MODULE_INIT, "Path: " + path + " does not contain a valid config file.")
            }
        }

        if (hasExistingConfig)
        {
            // Use the config path found by the checks above
            SetConfigDirPath(configPath);
            LOG(MODULE_INIT, "Using " + QV2RAY_CONFIG_DIR + " as the config path.")
        }
        else
        {
            // If there's no existing config.
            //
            // Create new config at these dirs, these are default values for each
            // platform.
#if defined(Q_OS_WIN) && !defined(QV2RAY_NO_ASIDECONFIG)
            configPath = currentPathConfig;
#else
            configPath = configQv2ray;
#endif
            bool mkpathResult = QDir().mkpath(configPath);
            bool hasPossibleNewLocation = mkpathResult && CheckSettingsPathAvailability(configPath, false);
            // Check if the dirs are write-able
            if (!hasPossibleNewLocation)
            {
                // None of the path above can be used as a dir for storing config.
                // Even the last folder failed to pass the check.
                LOG(MODULE_INIT, "FATAL")
                LOG(MODULE_INIT, " ---> CANNOT find a proper place to store Qv2ray config files.")
                QvMessageBoxWarn(nullptr, tr("Cannot Start Qv2ray"),
                                 tr("Cannot find a place to store config files.") + NEWLINE +                                          //
                                     tr("Qv2ray has searched these paths below:") + NEWLINE + NEWLINE +                                //
                                     configFilePaths.join(NEWLINE) + NEWLINE +                                                         //
                                     tr("It usually means you don't have the write permission to all of those locations.") + NEWLINE + //
                                     tr("Qv2ray will now exit."));                                                                     //
                return false;
            }
            // Found a valid config dir, with write permission, but assume no config is located in it.
            LOG(MODULE_INIT, "Set " + configPath + " as the config path.")
            SetConfigDirPath(configPath);

            if (QFile::exists(QV2RAY_CONFIG_FILE))
            {
                // As we already tried to load config from every possible dir.
                //
                // This condition branch (!hasExistingConfig check) holds the fact that current config dir,
                // should NOT contain any valid file (at least in the same name)
                //
                // It usually means that QV2RAY_CONFIG_FILE here has a corrupted JSON format.
                //
                // Otherwise Qv2ray would have loaded this config already instead of notifying to create a new config in this folder.
                //
                LOG(MODULE_INIT, "This should not occur: Qv2ray config exists but failed to load.")
                QvMessageBoxWarn(nullptr, tr("Failed to initialise Qv2ray"),
                                 tr("Failed to determine the location of config file:") + NEWLINE +                                   //
                                     tr("Qv2ray has found a config file, but it failed to be loaded due to some errors.") + NEWLINE + //
                                     tr("A workaround is to remove the this file and restart Qv2ray:") + NEWLINE +                    //
                                     QV2RAY_CONFIG_FILE + NEWLINE +                                                                   //
                                     tr("Qv2ray will now exit.") + NEWLINE +                                                          //
                                     tr("Please report if you think it's a bug."));                                                   //
                return false;
            }

            Qv2rayConfigObject conf;
            conf.kernelConfig.KernelPath(QString(QV2RAY_DEFAULT_VCORE_PATH));
            conf.kernelConfig.AssetsPath(QString(QV2RAY_DEFAULT_VASSETS_PATH));
            conf.logLevel = 3;
            conf.uiConfig.language = QLocale::system().name();
            conf.defaultRouteConfig.dnsConfig.servers << QvConfig_DNS::DNSServerObject{ "1.1.1.1" } //
                                                      << QvConfig_DNS::DNSServerObject{ "8.8.8.8" } //
                                                      << QvConfig_DNS::DNSServerObject{ "8.8.4.4" };

            // Save initial config.
            SaveGlobalSettings(conf);
            LOG(MODULE_INIT, "Created initial config file.")
        }

        if (!QDir(QV2RAY_GENERATED_DIR).exists())
        {
            // The dir used to generate final config file, for V2ray interaction.
            QDir().mkdir(QV2RAY_GENERATED_DIR);
            LOG(MODULE_INIT, "Created config generation dir at: " + QV2RAY_GENERATED_DIR)
        }

        return true;
    }

    bool Qv2rayApplication::CheckSettingsPathAvailability(const QString &_path, bool checkExistingConfig)
    {
        auto path = _path;

        if (!path.endsWith("/"))
            path.append("/");

        // Does not exist.
        if (!QDir(path).exists())
            return false;

        {
            // A temp file used to test file permissions in that folder.
            QFile testFile(path + ".qv2ray_test_file" + QSTRN(QTime::currentTime().msecsSinceStartOfDay()));

            if (!testFile.open(QFile::OpenModeFlag::ReadWrite))
            {
                LOG(MODULE_SETTINGS, "Directory at: " + path + " cannot be used as a valid config file path.")
                LOG(MODULE_SETTINGS, "---> Cannot create a new file or open a file for writing.")
                return false;
            }

            testFile.write("Qv2ray test file, feel free to remove.");
            testFile.flush();
            testFile.close();

            if (!testFile.remove())
            {
                // This is rare, as we can create a file but failed to remove it.
                LOG(MODULE_SETTINGS, "Directory at: " + path + " cannot be used as a valid config file path.")
                LOG(MODULE_SETTINGS, "---> Cannot remove a file.")
                return false;
            }
        }

        if (!checkExistingConfig)
        {
            // Just pass the test
            return true;
        }

        // Check if an existing config is found.
        QFile configFile(path + "Qv2ray.conf");

        // No such config file.
        if (!configFile.exists())
            return false;

        if (!configFile.open(QIODevice::ReadWrite))
        {
            LOG(MODULE_SETTINGS, "File: " + configFile.fileName() + " cannot be opened!")
            return false;
        }

        const auto err = VerifyJsonString(StringFromFile(configFile));
        if (!err.isEmpty())
        {
            LOG(MODULE_INIT, "Json parse returns: " + err)
            return false;
        }

        // If the file format is valid.
        const auto conf = JsonFromString(StringFromFile(configFile));
        LOG(MODULE_SETTINGS, "Found a config file, v=" + conf["config_version"].toString() + " path=" + path)
        configFile.close();
        return true;
    }

    bool Qv2rayApplication::PreInitilize(int argc, char *argv[])
    {
        QString errorMessage;

        {
            QCoreApplication coreApp(argc, argv);
            const auto &args = coreApp.arguments();
            Qv2rayProcessArgument.path = args.first();
            Qv2rayProcessArgument.version = QV2RAY_VERSION_STRING;
            Qv2rayProcessArgument.data = args.join(" ");
            switch (ParseCommandLine(&errorMessage))
            {
                case QUIT:
                {
                    return false;
                }
                case ERROR:
                {
                    LOG(MODULE_INIT, errorMessage)
                    return false;
                }
                case CONTINUE:
                {
                    break;
                }
            }
        }
        // noScaleFactors = disable HiDPI
        if (StartupOption.noScaleFactor)
        {
            LOG(MODULE_INIT, "Force set QT_SCALE_FACTOR to 1.")
            LOG(MODULE_UI, "Original QT_SCALE_FACTOR was: " + qEnvironmentVariable("QT_SCALE_FACTOR"))
            qputenv("QT_SCALE_FACTOR", "1");
        }
        else
        {
            LOG(MODULE_INIT, "High DPI scaling is enabled.")
            QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
        }
        return true;
    }

    Qv2rayApplication::commandline_status Qv2rayApplication::ParseCommandLine(QString *errorMessage)
    {
        QCommandLineParser parser;
        QCommandLineOption noAPIOption("noAPI", tr("Disable gRPC API subsystem."));
        QCommandLineOption noPluginsOption("noPlugin", tr("Disable plugins feature"));
        QCommandLineOption noScaleFactorOption("noScaleFactor", tr("Disable Qt UI scale factor"));
        QCommandLineOption debugOption("debug", tr("Enable debug output"));

        parser.setApplicationDescription(tr("Qv2ray - A cross-platform Qt frontend for V2ray."));
        parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
        //
        parser.addOption(noAPIOption);
        parser.addOption(noPluginsOption);
        parser.addOption(noScaleFactorOption);
        parser.addOption(debugOption);
        auto helpOption = parser.addHelpOption();
        auto versionOption = parser.addVersionOption();

        if (!parser.parse(arguments()))
        {
            *errorMessage = parser.errorText();
            return ERROR;
        }

        if (parser.isSet(versionOption))
        {
            parser.showVersion();
            return QUIT;
        }

        if (parser.isSet(helpOption))
        {
            parser.showHelp();
            return QUIT;
        }

        if (parser.isSet(noAPIOption))
        {
            DEBUG(MODULE_INIT, "noAPIOption is set.")
            StartupOption.noAPI = true;
        }

        if (parser.isSet(debugOption))
        {
            DEBUG(MODULE_INIT, "debugOption is set.")
            StartupOption.debugLog = true;
        }

        if (parser.isSet(noScaleFactorOption))
        {
            DEBUG(MODULE_INIT, "noScaleFactorOption is set.")
            StartupOption.noScaleFactor = true;
        }

        if (parser.isSet(noPluginsOption))
        {
            DEBUG(MODULE_INIT, "noPluginOption is set.")
            StartupOption.noPlugins = true;
        }

        return CONTINUE;
    }

} // namespace Qv2ray