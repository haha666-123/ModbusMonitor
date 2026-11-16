#pragma once
// ============================================================================
//  AI 诊断
//  ---------------------------------------------------------------------------
//  越限告警时，把 “最近数据序列 + 设备上下文” 交给大模型，拿回诊断建议。
//
//  两种模式：
//    1) 已配置 API Key : 走 OpenAI 兼容的 /chat/completions 接口，
//       第一轮带上 query_history 工具声明；若模型返回 tool_calls，
//       本地执行历史查询并把结果回传，第二轮生成最终结论。
//       —— 这就是最小可用的 Function Calling / Agent 工作流。
//    2) 未配置 API Key : 走本地规则兜底，保证程序在无网络时也能跑通演示。
//
//  设计要点：
//    * AI 是 “旁路”，异步、失败不影响采集主链路；
//    * 用户常常把「网页控制台地址」当成 API 端点填进来
//      （如 platform.deepseek.com/api_keys），这里做地址规范化 + 友好报错。
// ============================================================================
#include "../common/ModbusDefs.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;
class QJsonArray;

namespace mb {

class AiDiagnoser : public QObject {
    Q_OBJECT
public:
    using HistoryProvider = std::function<std::vector<DataPoint>(quint16 addr, int maxCount)>;

    //! 代理模式：部分环境有需要认证的系统代理，会拦截 HTTPS 导致 HTTP/2 报
    //  “Host requires authentication”，此时可切换为 NoProxy
    enum class ProxyMode { System, None };

    explicit AiDiagnoser(QObject* parent = nullptr);

    void configure(const QUrl& endpoint, const QString& apiKey, const QString& model);
    void setProxyMode(ProxyMode mode);
    [[nodiscard]] ProxyMode proxyMode() const { return m_proxyMode; }

    [[nodiscard]] bool enabled() const { return !m_apiKey.isEmpty(); }

    void setHistoryProvider(HistoryProvider provider) { m_history = std::move(provider); }

    //! 发起一次诊断（异步，结果通过 diagnosisReady 返回）
    void diagnose(qint64 alarmId, const Alarm& alarm, const std::vector<DataPoint>& context);

    // ---------------------------------------------------------------- 地址规范化
    //! 把用户填的地址补全成 /chat/completions 端点；空值返回默认端点
    [[nodiscard]] static QUrl normalizeUrl(const QUrl& input);

    //! 返回空串表示地址可用；否则返回给用户看的修正建议
    [[nodiscard]] static QString endpointProblem(const QUrl& url);

    //! 当前生效的（规范化后的）端点，供界面回显
    [[nodiscard]] QUrl endpoint() const { return m_endpoint; }

signals:
    void diagnosisReady(qint64 alarmId, const QString& text);

private:
    void post(const QJsonArray& messages, qint64 alarmId, int round,
              const Alarm& alarm, const std::vector<DataPoint>& context);
    void onReply(QNetworkReply* reply, qint64 alarmId, int round,
                 const Alarm& alarm, const std::vector<DataPoint>& context);

    QString localFallback(const Alarm& alarm) const;
    QString contextToText(const std::vector<DataPoint>& context) const;
    static QString sanitizeKey(const QString& raw);

    QNetworkAccessManager* m_nam = nullptr;
    QUrl    m_endpoint;
    QString m_endpointProblem;      // 非空表示配置有问题，诊断时直接降级并提示
    QString m_apiKey;
    QString m_model = QStringLiteral("gpt-4o-mini");
    ProxyMode m_proxyMode = ProxyMode::System;
    HistoryProvider m_history;
};

} // namespace mb
