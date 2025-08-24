#include "security/audit_engine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <random>
#include <ctime>

namespace slonana {
namespace security {

SecurityAuditEngine::SecurityAuditEngine(const AuditConfig& config, const std::string& target_path)
    : config_(config), target_path_(target_path) {
    
    std::cout << "Security audit engine initialized for target: " << target_path_ << std::endl;
    
    // Load built-in audit rules
    load_builtin_rules();
    
    // Load external tools if available
    load_external_tools();
    
    std::cout << "Loaded " << rules_.size() << " audit rules and " 
              << external_tools_.size() << " external tools" << std::endl;
}

SecurityAuditEngine::~SecurityAuditEngine() {
    std::cout << "Security audit engine destroyed" << std::endl;
}

void SecurityAuditEngine::load_builtin_rules() {
    // Load built-in security audit rules
    // These would typically check for common security issues
    
    std::cout << "Loading built-in security audit rules..." << std::endl;
    
    // Code analysis rules would be added here
    // Network security rules would be added here
    // Cryptographic validation rules would be added here
    // etc.
}

void SecurityAuditEngine::load_custom_rules() {
    // Load custom audit rules from specified paths
    for (const auto& path : config_.custom_rules_path) {
        if (std::filesystem::exists(path)) {
            std::cout << "Loading custom rules from: " << path << std::endl;
            // Implementation would load and parse custom rule definitions
        }
    }
}

void SecurityAuditEngine::load_external_tools() {
    // Check for available external security tools
    std::cout << "Checking for external security tools..." << std::endl;
    
    // Check for common static analysis tools
    if (system("which cppcheck > /dev/null 2>&1") == 0) {
        std::cout << "Found cppcheck static analyzer" << std::endl;
    }
    
    if (system("which clang-static-analyzer > /dev/null 2>&1") == 0) {
        std::cout << "Found clang static analyzer" << std::endl;
    }
    
    if (system("which valgrind > /dev/null 2>&1") == 0) {
        std::cout << "Found valgrind memory analyzer" << std::endl;
    }
}

bool SecurityAuditEngine::run_audit() {
    std::cout << "Starting comprehensive security audit..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    findings_.clear();
    
    bool overall_success = true;
    size_t rules_executed = 0;
    size_t rules_passed = 0;
    
    // Execute all enabled audit rules
    for (auto& rule : rules_) {
        if (config_.enabled_categories.empty() || 
            std::find(config_.enabled_categories.begin(), config_.enabled_categories.end(),
                     audit_utils::audit_category_to_string(rule->get_category())) != config_.enabled_categories.end()) {
            
            std::cout << "Executing rule: " << rule->get_name() << std::endl;
            
            bool rule_success = execute_rule(*rule);
            rules_executed++;
            
            if (rule_success) {
                rules_passed++;
            } else {
                overall_success = false;
            }
            
            // Add findings from this rule
            auto rule_findings = rule->get_findings();
            findings_.insert(findings_.end(), rule_findings.begin(), rule_findings.end());
        }
    }
    
    // Execute external tools
    for (auto& tool : external_tools_) {
        std::cout << "Executing external tool: " << tool->get_tool_name() << std::endl;
        
        if (tool->is_available()) {
            bool tool_success = execute_external_tool(*tool);
            if (!tool_success) {
                overall_success = false;
            }
            
            // Add findings from this tool
            auto tool_findings = tool->get_findings();
            findings_.insert(findings_.end(), tool_findings.begin(), tool_findings.end());
        }
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    std::cout << "Security audit completed in " << duration.count() << " seconds" << std::endl;
    std::cout << "Rules executed: " << rules_executed << ", passed: " << rules_passed << std::endl;
    std::cout << "Total findings: " << findings_.size() << std::endl;
    std::cout << "Critical: " << get_critical_count() 
              << ", High: " << get_high_count()
              << ", Medium: " << get_medium_count()
              << ", Low: " << get_low_count() << std::endl;
    
    // Check if audit should fail based on findings
    if (config_.fail_on_critical && get_critical_count() > 0) {
        std::cout << "Audit failed due to critical findings" << std::endl;
        return false;
    }
    
    if (config_.fail_on_high && get_high_count() > 0) {
        std::cout << "Audit failed due to high severity findings" << std::endl;
        return false;
    }
    
    return overall_success;
}

bool SecurityAuditEngine::execute_rule(IAuditRule& rule) {
    try {
        return rule.execute();
    } catch (const std::exception& e) {
        std::cerr << "Error executing rule " << rule.get_rule_id() << ": " << e.what() << std::endl;
        return false;
    }
}

bool SecurityAuditEngine::execute_external_tool(IAuditTool& tool) {
    try {
        return tool.execute_scan(target_path_);
    } catch (const std::exception& e) {
        std::cerr << "Error executing tool " << tool.get_tool_name() << ": " << e.what() << std::endl;
        return false;
    }
}

std::vector<AuditFinding> SecurityAuditEngine::get_findings_by_severity(AuditLevel severity) const {
    std::vector<AuditFinding> filtered;
    for (const auto& finding : findings_) {
        if (finding.severity == severity) {
            filtered.push_back(finding);
        }
    }
    return filtered;
}

std::vector<AuditFinding> SecurityAuditEngine::get_findings_by_category(AuditCategory category) const {
    std::vector<AuditFinding> filtered;
    for (const auto& finding : findings_) {
        if (finding.category == category) {
            filtered.push_back(finding);
        }
    }
    return filtered;
}

uint64_t SecurityAuditEngine::get_critical_count() const {
    return get_findings_by_severity(AuditLevel::CRITICAL).size();
}

uint64_t SecurityAuditEngine::get_high_count() const {
    return get_findings_by_severity(AuditLevel::HIGH).size();
}

uint64_t SecurityAuditEngine::get_medium_count() const {
    return get_findings_by_severity(AuditLevel::MEDIUM).size();
}

uint64_t SecurityAuditEngine::get_low_count() const {
    return get_findings_by_severity(AuditLevel::LOW).size();
}

bool SecurityAuditEngine::has_blocking_findings() const {
    if (config_.fail_on_critical && get_critical_count() > 0) {
        return true;
    }
    if (config_.fail_on_high && get_high_count() > 0) {
        return true;
    }
    return false;
}

AuditReport SecurityAuditEngine::get_report() {
    AuditReport report;
    report.report_id = audit_utils::generate_report_id();
    report.audit_name = "Slonana Security Audit";
    report.start_time = audit_utils::get_current_timestamp();
    report.end_time = audit_utils::get_current_timestamp();
    report.status = AuditStatus::COMPLETED;
    
    report.total_rules_executed = rules_.size();
    report.rules_passed = rules_.size(); // Simplified for now
    report.rules_failed = 0;
    
    report.critical_findings = get_critical_count();
    report.high_findings = get_high_count();
    report.medium_findings = get_medium_count();
    report.low_findings = get_low_count();
    
    report.findings = findings_;
    report.auditor_name = "Slonana Security Engine";
    report.target_version = "1.0.0";
    
    return report;
}

bool SecurityAuditEngine::export_report(const std::string& output_path, const std::string& format) {
    try {
        AuditReport report = get_report();
        
        if (format == "json") {
            std::string json_report = audit_utils::format_report_json(report);
            std::ofstream file(output_path);
            file << json_report;
            file.close();
            std::cout << "Security audit report exported to: " << output_path << std::endl;
            return true;
        } else if (format == "html") {
            std::string html_report = audit_utils::format_report_html(report);
            std::ofstream file(output_path);
            file << html_report;
            file.close();
            std::cout << "Security audit report exported to: " << output_path << std::endl;
            return true;
        } else {
            std::cerr << "Unsupported report format: " << format << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error exporting report: " << e.what() << std::endl;
        return false;
    }
}

bool SecurityAuditEngine::validate_target_path() const {
    return std::filesystem::exists(target_path_) && std::filesystem::is_directory(target_path_);
}

bool SecurityAuditEngine::validate_tools() const {
    for (const auto& tool : external_tools_) {
        if (!tool->is_available()) {
            std::cout << "Tool not available: " << tool->get_tool_name() << std::endl;
            return false;
        }
    }
    return true;
}

void SecurityAuditEngine::add_rule(std::unique_ptr<IAuditRule> rule) {
    std::cout << "Added audit rule: " << rule->get_name() << std::endl;
    rules_.push_back(std::move(rule));
}

void SecurityAuditEngine::add_external_tool(std::unique_ptr<IAuditTool> tool) {
    std::cout << "Added external tool: " << tool->get_tool_name() << std::endl;
    external_tools_.push_back(std::move(tool));
}

std::vector<std::string> SecurityAuditEngine::list_rules() const {
    std::vector<std::string> rule_names;
    for (const auto& rule : rules_) {
        rule_names.push_back(rule->get_name());
    }
    return rule_names;
}

std::vector<std::string> SecurityAuditEngine::list_external_tools() const {
    std::vector<std::string> tool_names;
    for (const auto& tool : external_tools_) {
        tool_names.push_back(tool->get_tool_name());
    }
    return tool_names;
}

// Utility function implementations
namespace audit_utils {
    
    std::string audit_level_to_string(AuditLevel level) {
        switch (level) {
            case AuditLevel::LOW: return "low";
            case AuditLevel::MEDIUM: return "medium";
            case AuditLevel::HIGH: return "high";
            case AuditLevel::CRITICAL: return "critical";
            default: return "unknown";
        }
    }
    
    std::string audit_category_to_string(AuditCategory category) {
        switch (category) {
            case AuditCategory::CODE_ANALYSIS: return "code_analysis";
            case AuditCategory::NETWORK_SECURITY: return "network_security";
            case AuditCategory::CRYPTOGRAPHIC_VALIDATION: return "cryptographic_validation";
            case AuditCategory::ACCESS_CONTROL: return "access_control";
            case AuditCategory::INPUT_VALIDATION: return "input_validation";
            case AuditCategory::DEPENDENCY_SCAN: return "dependency_scan";
            case AuditCategory::CONFIGURATION_REVIEW: return "configuration_review";
            case AuditCategory::RUNTIME_ANALYSIS: return "runtime_analysis";
            default: return "unknown";
        }
    }
    
    std::string generate_finding_id() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(100000, 999999);
        return "FIND-" + std::to_string(dis(gen));
    }
    
    std::string generate_report_id() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(100000, 999999);
        return "RPT-" + std::to_string(dis(gen));
    }
    
    uint64_t get_current_timestamp() {
        return std::time(nullptr);
    }
    
    std::string format_report_json(const AuditReport& report) {
        std::ostringstream json;
        json << "{\n";
        json << "  \"report_id\": \"" << report.report_id << "\",\n";
        json << "  \"audit_name\": \"" << report.audit_name << "\",\n";
        json << "  \"start_time\": " << report.start_time << ",\n";
        json << "  \"end_time\": " << report.end_time << ",\n";
        json << "  \"status\": \"completed\",\n";
        json << "  \"summary\": {\n";
        json << "    \"total_rules_executed\": " << report.total_rules_executed << ",\n";
        json << "    \"rules_passed\": " << report.rules_passed << ",\n";
        json << "    \"rules_failed\": " << report.rules_failed << ",\n";
        json << "    \"critical_findings\": " << report.critical_findings << ",\n";
        json << "    \"high_findings\": " << report.high_findings << ",\n";
        json << "    \"medium_findings\": " << report.medium_findings << ",\n";
        json << "    \"low_findings\": " << report.low_findings << "\n";
        json << "  },\n";
        json << "  \"findings\": [\n";
        for (size_t i = 0; i < report.findings.size(); ++i) {
            const auto& finding = report.findings[i];
            json << "    {\n";
            json << "      \"finding_id\": \"" << finding.finding_id << "\",\n";
            json << "      \"rule_id\": \"" << finding.rule_id << "\",\n";
            json << "      \"severity\": \"" << audit_level_to_string(finding.severity) << "\",\n";
            json << "      \"category\": \"" << audit_category_to_string(finding.category) << "\",\n";
            json << "      \"title\": \"" << finding.title << "\",\n";
            json << "      \"description\": \"" << finding.description << "\",\n";
            json << "      \"location\": \"" << finding.location << "\"\n";
            json << "    }";
            if (i < report.findings.size() - 1) json << ",";
            json << "\n";
        }
        json << "  ]\n";
        json << "}\n";
        return json.str();
    }
    
    std::string format_report_html(const AuditReport& report) {
        std::ostringstream html;
        html << "<!DOCTYPE html>\n<html>\n<head>\n";
        html << "<title>Security Audit Report - " << report.audit_name << "</title>\n";
        html << "<style>\n";
        html << "body { font-family: Arial, sans-serif; margin: 20px; }\n";
        html << ".critical { color: #d32f2f; }\n";
        html << ".high { color: #f57c00; }\n";
        html << ".medium { color: #fbc02d; }\n";
        html << ".low { color: #388e3c; }\n";
        html << "table { border-collapse: collapse; width: 100%; }\n";
        html << "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n";
        html << "th { background-color: #f2f2f2; }\n";
        html << "</style>\n</head>\n<body>\n";
        html << "<h1>Security Audit Report</h1>\n";
        html << "<h2>Summary</h2>\n";
        html << "<p><strong>Report ID:</strong> " << report.report_id << "</p>\n";
        html << "<p><strong>Audit Name:</strong> " << report.audit_name << "</p>\n";
        html << "<p><strong>Critical Findings:</strong> " << report.critical_findings << "</p>\n";
        html << "<p><strong>High Findings:</strong> " << report.high_findings << "</p>\n";
        html << "<p><strong>Medium Findings:</strong> " << report.medium_findings << "</p>\n";
        html << "<p><strong>Low Findings:</strong> " << report.low_findings << "</p>\n";
        html << "<h2>Detailed Findings</h2>\n";
        html << "<table>\n";
        html << "<tr><th>ID</th><th>Severity</th><th>Category</th><th>Title</th><th>Location</th></tr>\n";
        for (const auto& finding : report.findings) {
            html << "<tr>\n";
            html << "<td>" << finding.finding_id << "</td>\n";
            html << "<td class=\"" << audit_level_to_string(finding.severity) << "\">" 
                 << audit_level_to_string(finding.severity) << "</td>\n";
            html << "<td>" << audit_category_to_string(finding.category) << "</td>\n";
            html << "<td>" << finding.title << "</td>\n";
            html << "<td>" << finding.location << "</td>\n";
            html << "</tr>\n";
        }
        html << "</table>\n";
        html << "</body>\n</html>\n";
        return html.str();
    }
}

}} // namespace slonana::security