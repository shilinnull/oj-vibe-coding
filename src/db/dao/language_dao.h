#pragma once

#include <optional>
#include <string>
#include <vector>

#include "db/mysql_pool.h"
#include "model/language.h"

namespace oj {

class LanguageDao {
 public:
	explicit LanguageDao(MySqlPool& pool);

	std::vector<Language> ListAll(bool only_enabled);
	std::optional<Language> GetById(int id);

	int Create(const Language& lang);
	bool Update(int id, const Language& lang);
	bool SetEnabled(int id, bool enabled);

 private:
	MySqlPool& pool_;
};

}  // namespace oj
