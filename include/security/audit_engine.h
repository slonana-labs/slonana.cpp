#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <chrono>

namespace slonana {
namespace security {

using namespace slonana::common;

enum class AuditLevel {
    LOW,
    MEDIUM,
    HIGH,
    CRITICAL
};

enum class AuditCategory {
    CODE_ANALYSIS,
    NETWORK_SECURITY,
    CRYPTOGRAPHIC_VALIDATION,
    ACCESS_CONTROL,
    INPUT_VALIDATION,
    DEPENDENCY_SCAN,
    CONFIGURATION_REVIEW,
    RUNTIME_ANALYSIS
};

enum class AuditStatus {
    PENDING,
    IN_PROGRESS,
    COMPLETED,
    FAILED,
    SKIPPED
};

struct AuditRule {
    std::string rule_id;
    std::string name;
    std::string description;
    AuditCategory category;
    AuditLevel severity;
    std::function<bool()> checker;
    std::vector<std::string> tags;
    bool enabled;
};

struct AuditFinding {
    std::string finding_id;
    std::string rule_id;
    AuditLevel severity;
    AuditCategory category;
    std::string title;
    std::string description;
    std::string location; // file:line or component
    std::string evidence;
    std::vector<std::string> recommendations;
    uint64_t timestamp;
    bool is_false_positive;
    std::string remediation_status;
};

struct AuditReport {
    std::string report_id;
    std::string audit_name;
    uint64_t start_time;
    uint64_t end_time;
    AuditStatus status;
    
    // Summary statistics
    uint64_t total_rules_executed;
    uint64_t rules_passed;
    uint64_t rules_failed;
    uint64_t critical_findings;
    uint64_t high_findings;
    uint64_t medium_findings;
    uint64_t low_findings;
    
    // Detailed findings
    std::vector<AuditFinding> findings;
    
    // Metadata
    std::string auditor_name;
    std::string target_version;
    std::unordered_map<std::string, std::string> metadata;
};

struct AuditConfig {
    std::vector<std::string> enabled_categories;
    AuditLevel minimum_severity = AuditLevel::LOW;
    bool fail_on_critical = true;
    bool fail_on_high = false;
    bool include_code_analysis = true;
    bool include_dependency_scan = true;
    bool include_network_security = true;
    bool include_crypto_validation = true;
    std::vector<std::string> excluded_rules;
    std::vector<std::string> custom_rules_path;
    int max_execution_time_minutes = 60;
    bool generate_detailed_report = true;
    std::string output_format = "json"; // json, xml, html, pdf
};

// Interface for audit rule implementations
class IAuditRule {
public:
    virtual ~IAuditRule() = default;
    virtual std::string get_rule_id() const = 0;
    virtual std::string get_name() const = 0;
    virtual std::string get_description() const = 0;
    virtual AuditCategory get_category() const = 0;
    virtual AuditLevel get_severity() const = 0;
    virtual bool execute() = 0;
    virtual std::vector<AuditFinding> get_findings() const = 0;
};

// Interface for external audit tool integration
class IAuditTool {
public:
    virtual ~IAuditTool() = default;
    virtual std::string get_tool_name() const = 0;
    virtual std::string get_version() const = 0;
    virtual bool is_available() const = 0;
    virtual bool execute_scan(const std::string& target_path) = 0;
    virtual std::vector<AuditFinding> get_findings() const = 0;
    virtual bool supports_category(AuditCategory category) const = 0;
};

// Main security audit engine
class SecurityAuditEngine {
private:
    AuditConfig config_;
    std::vector<std::unique_ptr<IAuditRule>> rules_;
    std::vector<std::unique_ptr<IAuditTool>> external_tools_;
    std::vector<AuditFinding> findings_;
    std::string target_path_;
    
    // Built-in rule implementations
    void load_builtin_rules();
    void load_custom_rules();
    void load_external_tools();
    
    // Rule execution
    bool execute_rule(IAuditRule& rule);
    bool execute_external_tool(IAuditTool& tool);
    
    // Reporting
    AuditReport generate_report();
    void export_report(const AuditReport& report, const std::string& format, const std::string& output_path);

public:
    SecurityAuditEngine(const AuditConfig& config, const std::string& target_path);
    ~SecurityAuditEngine();
    
    // Configuration
    void set_config(const AuditConfig& config) { config_ = config; }
    AuditConfig get_config() const { return config_; }
    void set_target_path(const std::string& path) { target_path_ = path; }
    
    // Rule management
    void add_rule(std::unique_ptr<IAuditRule> rule);
    void remove_rule(const std::string& rule_id);
    void enable_rule(const std::string& rule_id);
    void disable_rule(const std::string& rule_id);
    std::vector<std::string> list_rules() const;
    
    // External tool management
    void add_external_tool(std::unique_ptr<IAuditTool> tool);
    std::vector<std::string> list_external_tools() const;
    
    // Audit execution
    bool run_audit();
    bool run_category(AuditCategory category);
    bool run_rule(const std::string& rule_id);
    
    // Results and reporting
    std::vector<AuditFinding> get_findings() const { return findings_; }
    std::vector<AuditFinding> get_findings_by_severity(AuditLevel severity) const;
    std::vector<AuditFinding> get_findings_by_category(AuditCategory category) const;
    AuditReport get_report();
    bool export_report(const std::string& output_path, const std::string& format = "json");
    
    // Statistics
    uint64_t get_critical_count() const;
    uint64_t get_high_count() const;
    uint64_t get_medium_count() const;
    uint64_t get_low_count() const;
    bool has_blocking_findings() const;
    
    // Validation
    bool validate_target_path() const;
    bool validate_tools() const;
    std::vector<std::string> get_missing_dependencies() const;
};

// Utility functions
namespace audit_utils {
    std::string audit_level_to_string(AuditLevel level);
    AuditLevel string_to_audit_level(const std::string& level);
    std::string audit_category_to_string(AuditCategory category);
    AuditCategory string_to_audit_category(const std::string& category);
    std::string audit_status_to_string(AuditStatus status);
    AuditStatus string_to_audit_status(const std::string& status);
    std::string generate_finding_id();
    std::string generate_report_id();
    uint64_t get_current_timestamp();
    bool is_critical_finding(const AuditFinding& finding);
    std::vector<AuditFinding> filter_findings_by_severity(const std::vector<AuditFinding>& findings, AuditLevel min_severity);
    std::string format_report_json(const AuditReport& report);
    std::string format_report_html(const AuditReport& report);
}

}} // namespace slonana::security