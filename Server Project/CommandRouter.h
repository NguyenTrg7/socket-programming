#ifndef COMMAND_ROUTER_H
#define COMMAND_ROUTER_H

#include "../Common/Session.h"
#include <vector>
#include <string>

void handleUser(const std::vector<std::string>& args, ServerSession& session);
void handlePass(const std::vector<std::string>& args, ServerSession& session);
void handleNoop(const std::vector<std::string>& args, ServerSession& session);
void handleQuit(const std::vector<std::string>& args, ServerSession& session);
void handleHelp(const std::vector<std::string>& args, ServerSession& session);
void handleAbort(const std::vector<std::string>& args, ServerSession& session);

void handlePwd(const std::vector<std::string>& args, ServerSession& session);
void handleCwd(const std::vector<std::string>& args, ServerSession& session);
void handleCdup(const std::vector<std::string>& args, ServerSession& session);
void handleMkd(const std::vector<std::string>& args, ServerSession& session);
void handleRmd(const std::vector<std::string>& args, ServerSession& session);

void handleList(const std::vector<std::string>& args, ServerSession& session);
void handleNlst(const std::vector<std::string>& args, ServerSession& session);
void handleStat(const std::vector<std::string>& args, ServerSession& session);
void handleSize(const std::vector<std::string>& args, ServerSession& session);
void handleMdtm(const std::vector<std::string>& args, ServerSession& session);
void handleHash(const std::vector<std::string>& args, ServerSession& session);

void handleDele(const std::vector<std::string>& args, ServerSession& session);
void handleRnfr(const std::vector<std::string>& args, ServerSession& session);
void handleRnto(const std::vector<std::string>& args, ServerSession& session);

void handleType(const std::vector<std::string>& args, ServerSession& session);
void handleMode(const std::vector<std::string>& args, ServerSession& session);
void handlePort(const std::vector<std::string>& args, ServerSession& session);
void handlePasv(const std::vector<std::string>& args, ServerSession& session);

void handleRetr(const std::vector<std::string>& args, ServerSession& session);
void handleStorAppeStou(const std::vector<std::string>& args, ServerSession& session, const std::string& cmd);

void routeCommand(const std::vector<std::string>& args, ServerSession& session);

#endif