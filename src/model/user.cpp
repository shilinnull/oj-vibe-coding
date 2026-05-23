#include "model/user.h"

namespace oj {

void to_json(nlohmann::json& j, const User& u) {
	j = nlohmann::json{{"id", u.id},
										 {"username", u.username},
										 {"password", u.password},
										 {"email", u.email},
										 {"role", u.role},
										 {"status", u.status},
										 {"created_at", u.created_at},
										 {"updated_at", u.updated_at}};
}

void from_json(const nlohmann::json& j, User& u) {
	u.id = j.value("id", 0LL);
	u.username = j.value("username", "");
	u.password = j.value("password", "");
	u.email = j.value("email", "");
	u.role = j.value("role", "student");
	u.status = j.value("status", "active");
	u.created_at = j.value("created_at", "");
	u.updated_at = j.value("updated_at", "");
}

}  // namespace oj
