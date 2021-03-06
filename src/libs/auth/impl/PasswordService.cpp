/*
 * Copyright (C) 2019 Emeric Poupon
 *
 * This file is part of LMS.
 *
 * LMS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LMS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PasswordService.hpp"

#include <Wt/Auth/HashFunction.h>
#include <Wt/Auth/PasswordStrengthValidator.h>
#include <Wt/WRandom.h>

#include "database/Session.hpp"
#include "utils/Exception.hpp"
#include "utils/Logger.hpp"
#ifdef LMS_SUPPORT_PAM
#include "pam/PAM.hpp"
#endif

namespace Auth {

std::unique_ptr<IPasswordService> createPasswordService(std::size_t maxThrottlerEntries)
{
	return std::make_unique<PasswordService>(maxThrottlerEntries);
}

PasswordService::PasswordService(std::size_t maxThrottlerEntries)
: _loginThrottler{maxThrottlerEntries}
{
}

bool
PasswordService::isAuthModeSupported(Database::User::AuthMode authMode) const
{
	switch (authMode)
	{
		case Database::User::AuthMode::Internal:
			return true;

		case Database::User::AuthMode::PAM:
#ifdef LMS_SUPPORT_PAM
			return true;
#else
			return false;
#endif
	}
	return false;
}

static
bool
checkUserPassword(Database::Session& session, const std::string& loginName, const std::string& password)
{
	Database::User::AuthMode authMode;
	Database::User::PasswordHash passwordHash;
	{
		auto transaction {session.createSharedTransaction()};

		const Database::User::pointer user {Database::User::getByLoginName(session, loginName)};
		if (!user)
			return false;

		authMode = user->getAuthMode();
		passwordHash = user->getPasswordHash();
	}

	switch (authMode)
	{
		case Database::User::AuthMode::Internal:
		{
			LMS_LOG(AUTH, DEBUG) << "Checking internal password for user '" << loginName << "'";
			const Wt::Auth::BCryptHashFunction hashFunc {7}; // TODO parametrize this
			return hashFunc.verify(password, passwordHash.salt, passwordHash.hash);
		}

		case Database::User::AuthMode::PAM:
#ifdef LMS_SUPPORT_PAM
			return PAM::checkUserPassword(loginName, password);
#else
			return false;
#endif
	}

	return false;
}

PasswordService::PasswordCheckResult
PasswordService::checkUserPassword(Database::Session& session, const boost::asio::ip::address& clientAddress, const std::string& loginName, const std::string& password)
{
	// Do not waste too much resource on brute force attacks (optim)
	{
		std::shared_lock<std::shared_timed_mutex> lock {_mutex};

		if (_loginThrottler.isClientThrottled(clientAddress))
			return PasswordCheckResult::Throttled;
	}

	const bool match {Auth::checkUserPassword(session, loginName, password)};
	{
		std::unique_lock<std::shared_timed_mutex> lock {_mutex};

		if (_loginThrottler.isClientThrottled(clientAddress))
			return PasswordCheckResult::Throttled;

		if (match)
		{
			_loginThrottler.onGoodClientAttempt(clientAddress);
			return PasswordCheckResult::Match;
		}
		else
		{
			_loginThrottler.onBadClientAttempt(clientAddress);
			return PasswordCheckResult::Mismatch;
		}
	}
}

Database::User::PasswordHash
PasswordService::hashPassword(const std::string& password) const
{
	const std::string salt {Wt::WRandom::generateId(32)};

	const Wt::Auth::BCryptHashFunction hashFunc {6};
	return {salt, hashFunc.compute(password, salt)};
}

bool
PasswordService::evaluatePasswordStrength(const std::string& loginName, const std::string& password) const
{
	Wt::Auth::PasswordStrengthValidator validator;
	validator.setMinimumLength(Wt::Auth::PasswordStrengthType::OneCharClass, 4);
	validator.setMinimumLength(Wt::Auth::PasswordStrengthType::TwoCharClass, 4);
	validator.setMinimumLength(Wt::Auth::PasswordStrengthType::PassPhrase, 4);
	validator.setMinimumLength(Wt::Auth::PasswordStrengthType::ThreeCharClass, 4);
	validator.setMinimumLength(Wt::Auth::PasswordStrengthType::FourCharClass, 4);
	validator.setMinimumPassPhraseWords(1);
	validator.setMinimumMatchLength(3);

	return validator.evaluateStrength(password, loginName, "").isValid();
}

} // namespace Auth

