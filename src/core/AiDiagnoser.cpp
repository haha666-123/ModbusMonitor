#include "AiDiagnoser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace mb {

namespace {
constexpr int kContextPoints = 20;
const QString kDefaultEndpoint = QStringLiteral("https://api.openai.com/v1/chat/completions");
const QString kSuffix = QStringLiteral("/chat/completions");
} // namespace

AiDiagnoser::AiDiagnoser(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    // 全局传输超时，避免网络不通报错迟迟不返回
    m_nam->setTransferTimeout(20000);
}

//! 用户粘贴 Key 时常带上引号、"Bearer " 前缀或换行，这里统一清掉
QString AiDiagnoser::sanitizeKey(const QString& raw)
{
    QString k = raw.trimmed();

    // 去掉包裹的引号
    if ((k.startsWith(QLatin1Char('"')) && k.endsWith(QLatin1Char('"'))) ||
        (k.startsWith(QLatin1Char('\'')) && k.endsWith(QLatin1Char('\'')))) {
        k = k.mid(1, k.size() - 2).trimmed();
    }
    // 去掉误粘贴的 Bearer 前缀（我们在请求头里会自己加）
    if (k.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive))
        k = k.mid(7).trimmed();

    return k;
}

void AiDiagnoser::setProxyMode(ProxyMode mode)
{
    m_proxyMode = mode;
    if (!m_nam)
        return;

    if (mode == ProxyMode::None) {
        m_nam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    } else {
        m_nam->setProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
    }
}

// ---------------------------------------------------------------------------
//  地址规范化
//  用户常见的三种填法：
//     https://api.deepseek.com                        -> 补成 .../chat/completions
//     https://api.deepseek.com/v1                     -> 补成 .../v1/chat/completions
//     https://platform.deepseek.com/api_keys          -> 这是网页控制台，拒绝并提示
// ---------------------------------------------------------------------------
QUrl AiDiagnoser::normalizeUrl(const QUrl& input)
{
    const QString raw = input.toString().trimmed();
    if (raw.isEmpty())
        return QUrl(kDefaultEndpoint);

    QUrl u = input;
    u = u.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash);

    QString path = u.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);

    if (path.endsWith(kSuffix, Qt::CaseInsensitive)) {
        u.setPath(path);
        return u;
    }

    // 明显是网页地址的，不做拼接，交给 endpointProblem() 提示用户
    if (!endpointProblem(u).isEmpty())
        return u;

    u.setPath(path + kSuffix);
    return u;
}

QString AiDiagnoser::endpointProblem(const QUrl& url)
{
    const QString raw = url.toString().trimmed();
    if (raw.isEmpty())
        return QString();                       // 空 -> 用默认端点，不算问题

    if (!url.isValid())
        return QStringLiteral("接口地址不是合法的 URL。");

    if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 &&
        url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0)
        return QStringLiteral("接口地址需要以 https:// 开头。");

    const QString host = url.host().toLower();
    const QString path = url.path().toLower();

    // 网页控制台 / 密钥管理页 —— 这地方调不通任何 API
    if (host.startsWith(QStringLiteral("platform.")) ||
        path.contains(QStringLiteral("api_key")) ||
        path.contains(QStringLiteral("apikey")) ||
        path.contains(QStringLiteral("console"))) {
        return QStringLiteral(
                   "这看起来是网页控制台 / 密钥管理页，不是 API 端点。\n"
                   "DeepSeek 请填：https://api.deepseek.com/chat/completions\n"
                   "OpenAI  请填：https://api.openai.com/v1/chat/completions");
    }

    return QString();                           // 可自动补全后缀，视为可用
}

void AiDiagnoser::configure(const QUrl& endpoint, const QString& apiKey, const QString& model)
{
    m_endpointProblem = endpointProblem(endpoint);
    m_endpoint        = normalizeUrl(endpoint);
    m_apiKey          = sanitizeKey(apiKey);
    if (!model.trimmed().isEmpty())
        m_model = model.trimmed();
}

QString AiDiagnoser::localFallback(const Alarm& alarm) const
{
    // 没有 API Key 时的规则兜底：保证功能可演示，同时不假装是模型给的
    QString advice;
    if (alarm.isHigh) {
        advice = QStringLiteral("检测到 %1 超过上限 %2%3。可能原因：负载过大 / 散热不良 / "
                                "传感器漂移。建议先确认传感器读数是否合理，再检查设备工况。")
                     .arg(alarm.name)
                     .arg(alarm.limit, 0, 'f', 2)
                     .arg(alarm.unit);
    } else {
        advice = QStringLiteral("检测到 %1 低于下限 %2%3。可能原因：供料不足 / 泄漏 / "
                                "传感器故障。建议检查供给端与传感器接线。")
                     .arg(alarm.name)
                     .arg(alarm.limit, 0, 'f', 2)
                     .arg(alarm.unit);
    }
    return QStringLiteral("[本地规则诊断] ") + advice;
}

QString AiDiagnoser::contextToText(const std::vector<DataPoint>& context) const
{
    if (context.empty())
        return QStringLiteral("(无历史数据)");

    QStringList parts;
    parts.reserve(static_cast<int>(context.size()));
    for (const DataPoint& p : context) {
        parts << QStringLiteral("%1=%2").arg(p.ts).arg(p.value, 0, 'f', 2);
    }
    return parts.join(QStringLiteral(", "));
}

void AiDiagnoser::diagnose(qint64 alarmId, const Alarm& alarm,
                           const std::vector<DataPoint>& context)
{
    if (!enabled()) {
        // 延迟一点返回，模拟异步链路，界面逻辑与真实调用保持一致
        QTimer::singleShot(200, this, [this, alarmId, text = localFallback(alarm)] {
            emit diagnosisReady(alarmId, text);
        });
        return;
    }

    // 地址明显填错时，不发网络请求，直接把修正建议和本地诊断一起返回
    if (!m_endpointProblem.isEmpty()) {
        const QString text =
            QStringLiteral("[AI 未启用：接口地址配置有误]\n%1\n\n%2")
                .arg(m_endpointProblem, localFallback(alarm));
        QTimer::singleShot(0, this, [this, alarmId, text] {
            emit diagnosisReady(alarmId, text);
        });
        return;
    }

    const QString system =
        QStringLiteral("你是工业设备监控系统的诊断助手。根据给定的设备告警和历史数据序列，"
                       "用中文简洁回答：1) 最可能的原因；2) 建议的排查步骤。不超过 120 字。");

    const QString user =
        QStringLiteral("设备告警：\n"
                       "  测点：%1 (地址 %2)\n"
                       "  当前值：%3%4\n"
                       "  %5限：%6%4\n"
                       "  最近 %7 个采样点（时间戳=值）：\n  %8\n")
            .arg(alarm.name)
            .arg(alarm.addr)
            .arg(alarm.value, 0, 'f', 2)
            .arg(alarm.unit)
            .arg(alarm.isHigh ? QStringLiteral("上") : QStringLiteral("下"))
            .arg(alarm.limit, 0, 'f', 2)
            .arg(static_cast<int>(context.size()))
            .arg(contextToText(context));

    QJsonArray messages;
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                {QStringLiteral("content"), system}});
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), user}});

    post(messages, alarmId, 0, alarm, context);
}

void AiDiagnoser::post(const QJsonArray& messages, qint64 alarmId, int round,
                       const Alarm& alarm, const std::vector<DataPoint>& context)
{
    QJsonObject body;
    body.insert(QStringLiteral("model"), m_model);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("temperature"), 0.2);

    if (round == 0) {
        // 第一轮：声明可用的工具，让模型自己决定是否需要查历史
        QJsonObject params;
        params.insert(QStringLiteral("type"), QStringLiteral("object"));
        params.insert(QStringLiteral("properties"),
                      QJsonObject{
                          {QStringLiteral("addr"),
                           QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                       {QStringLiteral("description"), QStringLiteral("寄存器地址")}}},
                          {QStringLiteral("count"),
                           QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                       {QStringLiteral("description"), QStringLiteral("需要的历史采样点数量")}}},
                      });
        params.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("addr")});

        QJsonObject tool;
        tool.insert(QStringLiteral("type"), QStringLiteral("function"));
        tool.insert(QStringLiteral("function"),
                    QJsonObject{
                        {QStringLiteral("name"), QStringLiteral("query_history")},
                        {QStringLiteral("description"), QStringLiteral("查询某个寄存器最近的历史采样值")},
                        {QStringLiteral("parameters"), params},
                    });

        body.insert(QStringLiteral("tools"), QJsonArray{tool});
        body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    }

    QNetworkRequest req(m_endpoint);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_apiKey).toUtf8());
    req.setRawHeader("Accept", "application/json");

    // 关键：强制 HTTP/1.1。
    // 走 HTTP/2 时，401/407 会被 HTTP/2 层包装成
    //   "Host requires authentication" 这样的流错误，拿不到真实状态码，
    //   HTTP/1.1 下才能读到 401/407/429 并翻译成人话。
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);

    QNetworkReply* reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, alarmId, round, alarm, context] {
                onReply(reply, alarmId, round, alarm, context);
            });
}

void AiDiagnoser::onReply(QNetworkReply* reply, qint64 alarmId, int round,
                          const Alarm& alarm, const std::vector<DataPoint>& context)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        QString reason;
        if (status == 401)
            reason = QStringLiteral("API Key 无效或未正确填写（请确认粘贴完整、无多余空格或引号）");
        else if (status == 403)
            reason = QStringLiteral("API Key 无该模型权限，或账号被限制");
        else if (status == 407)
            reason = QStringLiteral("被代理要求认证。请在「AI 设置」里把「网络代理」改为「不使用代理」后重试");
        else if (status == 404)
            reason = QStringLiteral("接口地址不存在，请确认以 /chat/completions 结尾");
        else if (status == 429)
            reason = QStringLiteral("请求过于频繁或额度不足");
        else if (status >= 500)
            reason = QStringLiteral("服务端错误，可稍后重试");
        else if (status == 0)
            reason = QStringLiteral("网络不可达（DNS/代理/防火墙）。原始信息：%1")
                         .arg(reply->errorString());
        else
            reason = reply->errorString();

        emit diagnosisReady(
            alarmId,
            QStringLiteral("[AI 调用失败，已降级为本地规则诊断]\n"
                           "原因：%1%2\n"
                           "接口：%3\n\n%4")
                .arg(reason,
                     status ? QStringLiteral("（HTTP %1）").arg(status) : QString(),
                     m_endpoint.toString(),
                     localFallback(alarm)));
        return;
    }

    const QByteArray raw = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) {
        emit diagnosisReady(alarmId, QStringLiteral("[AI 响应解析失败] ") + localFallback(alarm));
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        emit diagnosisReady(alarmId, QStringLiteral("[AI 返回为空] ") + localFallback(alarm));
        return;
    }

    const QJsonObject message = choices.at(0).toObject()
                                    .value(QStringLiteral("message")).toObject();

    // ---- 工具调用分支：模型希望先查历史数据
    const QJsonArray toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
    if (round == 0 && !toolCalls.isEmpty()) {
        const QJsonObject call = toolCalls.at(0).toObject();
        const QString callId = call.value(QStringLiteral("id")).toString();
        const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
        const QString argsText = fn.value(QStringLiteral("arguments")).toString();

        const QJsonDocument argsDoc = QJsonDocument::fromJson(argsText.toUtf8());
        const QJsonObject args = argsDoc.object();
        const quint16 addr = static_cast<quint16>(args.value(QStringLiteral("addr"))
                                                      .toInt(static_cast<int>(alarm.addr)));
        const int count = args.value(QStringLiteral("count")).toInt(kContextPoints);

        // 本地执行工具：查历史（这就是 Agent 里 “工具执行” 那一步）
        std::vector<DataPoint> hist = context;
        if (m_history)
            hist = m_history(addr, count);

        const QString toolResult = contextToText(hist);

        QJsonArray messages;
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                    {QStringLiteral("content"),
                                     QStringLiteral("你是工业设备监控系统的诊断助手，请用中文回答，不超过 120 字。")}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"),
                                     QStringLiteral("设备 %1 越限，当前 %2%3，限值 %4%3。"
                                                    "历史采样：%5")
                                         .arg(alarm.name)
                                         .arg(alarm.value, 0, 'f', 2)
                                         .arg(alarm.unit)
                                         .arg(alarm.limit, 0, 'f', 2)
                                         .arg(toolResult)}});
        Q_UNUSED(callId);

        post(messages, alarmId, 1, alarm, hist);
        return;
    }

    // ---- 普通文本分支
    QString content = message.value(QStringLiteral("content")).toString().trimmed();
    if (content.isEmpty()) {
        // 兼容部分实现直接返回 text 字段
        content = choices.at(0).toObject().value(QStringLiteral("text")).toString().trimmed();
    }
    if (content.isEmpty())
        content = localFallback(alarm);

    emit diagnosisReady(alarmId, content);
}

} // namespace mb
