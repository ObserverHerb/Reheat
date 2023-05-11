#include <QCoreApplication>
#include <QTextStream>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <Windows.h>
#include <optional>

#undef ERROR

using namespace Qt::Literals::StringLiterals;

QTextStream out(stdout);

const QString JSON_KEY_TYPE=u"type"_s;
const QString JSON_KEY_MESSAGE=u"message"_s;

enum class ExitCode
{
	OK,
	ERROR,
	INVALID
};

struct Targets
{
	QString channel;
	QString window;
};

void Message(const QString &type,const QString &message)
{
	out << "[" << type << "] " << message << Qt::endl;
}

void SystemMessage(const QString &message)
{
	Message(u"System"_s,message);
}

void ErrorMessage(const QString &message)
{
	Message(u"Error"_s,message);
}

void UsageMessage()
{
	Message(u"Usage"_s,u"Reheat.exe [game window title] [channel ID]"_s);
}

std::optional<QJsonObject> ParseMessage(const QString &message)
{
	QJsonParseError parseError;
	QJsonDocument json=QJsonDocument::fromJson(message.toLocal8Bit(),&parseError);
	if (parseError.error != QJsonParseError::NoError)
	{
		ErrorMessage(parseError.errorString());
		return std::nullopt;
	}
	return json.object();
}

std::optional<Targets> SetTargets(QCoreApplication &application)
{
	QStringList arguments=application.arguments();
	if (arguments.size() < 2)
	{
		ErrorMessage(u"Target window title and channel required"_s);
		return std::nullopt;
	}
	if (arguments.size() < 3)
	{
		ErrorMessage(u"Target channel required"_s);
		return std::nullopt;
	}
	Targets targets={arguments.at(2),arguments.at(1)};
	SystemMessage(QString{R"(Targeting window "%1")"}.arg(targets.window));
	SystemMessage(QString{"Targeting channel %1"}.arg(targets.channel));
	return targets;
}

int main(int argc,char *argv[])
{
	QCoreApplication application(argc,argv);
	application.setOrganizationName(u"EngineeringDeck"_s);
	application.setApplicationName(u"Reheat"_s);

	std::optional<Targets> targets=SetTargets(application);
	if (!targets)
	{
		UsageMessage();
		return static_cast<int>(ExitCode::INVALID);
	}

	QWebSocket socket;
	socket.connect(&socket,&QWebSocket::errorOccurred,[&socket,&application](QAbstractSocket::SocketError error) {
		ErrorMessage(socket.errorString());
		application.exit(static_cast<int>(ExitCode::ERROR));
	});
	socket.connect(&socket,&QWebSocket::connected,[]() {
		SystemMessage(u"Connected!"_s);
	});
	socket.connect(&socket,&QWebSocket::textMessageReceived,[&targets](QString message) {
		std::optional<QJsonObject> payload=ParseMessage(message);
		if (!payload) return;

		const QString type=payload->value(JSON_KEY_TYPE).toString();
		if (type == "system")
		{
			SystemMessage(payload->value(JSON_KEY_MESSAGE).toString());
			return;
		}
		if (type != "click")
		{
			SystemMessage(QString{"Unknown payload type (%1)"}.arg(type));
			return;
		}

		HWND window=FindWindowA(nullptr,targets->window.toLocal8Bit().constData());
		if (!window)
		{
			ErrorMessage(u"Window not found"_s);
			return;
		}

		RECT dimensions={};
		GetClientRect(window,&dimensions);
		double clickX=dimensions.right*payload->value("x").toString().toDouble();
		double clickY=dimensions.bottom*payload->value("y").toString().toDouble();
		Message(u"Click"_s,QString{"X: %1, Y: %2"}.arg(QString::number(clickX),QString::number(clickY)));
		SetForegroundWindow(window);
		POINT screen={std::lrint(clickX),std::lrint(clickY)};
		ClientToScreen(window,&screen);
		SetCursorPos(screen.x,screen.y);
		INPUT inputs[2]={
			{
				.type=INPUT_MOUSE,
				.mi={
					.dx=screen.x,
					.dy=screen.y,
					.mouseData=0,
					.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_LEFTDOWN,
					.time=0
				}
			},
			{
				.type=INPUT_MOUSE,
				.mi={
					.dx=screen.x,
					.dy=screen.y,
					.mouseData=0,
					.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_LEFTUP,
					.time=0
				}
			}
		};
		SendInput(2,inputs,sizeof(INPUT));
	});

	QMetaObject::invokeMethod(&socket,[&socket,&targets]() {
		socket.open(QUrl{QString{"wss://heat-api.j38.net/channel/%1"}.arg(targets->channel)});
	},Qt::QueuedConnection);

	return application.exec();
}