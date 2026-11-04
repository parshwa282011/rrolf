// Copyright (C) 2024  Paul Johnson

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

// Outgoing email through an HTTPS API (the one of the SMTP service). Blocking:
// call it from a worker thread. What it needs, all in the master's env:
//   EMAIL_API_URL   where the request goes (POST). Default: SMTPMaster,
//                   https://api.smtpmaster.com/api/send-email
//   EMAIL_API_KEY   the key; it only ever goes to that URL
//   EMAIL_API_AUTH  the header line that carries it, {key} is replaced
//                   (default "X-API-Key: {key}", what SMTPMaster wants)
//   EMAIL_API_BODY  the JSON body; {from} {from_name} {to} {subject} {text} are
//                   replaced by JSON strings. Default is SMTPMaster's:
//                   {"from_email":..,"from_name":..,"to_email":..,"subject":..,"body":..}
//   EMAIL_FROM      the sender address, on a domain verified in the account
//   EMAIL_FROM_NAME the sender's name (default Rysteria)
// Any 2xx answer counts as sent.
// Or Amazon SES: EMAIL_PROVIDER=ses with AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY,
// AWS_REGION (default us-east-1) and EMAIL_FROM on a verified identity. The
// IAM user only needs ses:SendEmail.
// At most EMAIL_DAILY_LIMIT (default 50, 0 = no limit) emails go out per UTC
// day, counted since the master started.
int ms_email_enabled();
// 1 when today's emails are used up
int ms_email_daily_full();
// 0 when the service accepted it, -2 when the daily limit is used up
int ms_email_send(char const *to, char const *subject, char const *text);
