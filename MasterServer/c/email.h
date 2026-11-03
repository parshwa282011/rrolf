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
//   EMAIL_API_URL   where the request goes (POST)
//   EMAIL_API_KEY   the key; it only ever goes to that URL
//   EMAIL_API_AUTH  the header line that carries it, {key} is replaced
//                   (default "Authorization: Bearer {key}")
//   EMAIL_API_BODY  the JSON body, {from} {to} {subject} {text} are replaced by
//                   JSON strings (default {"from":{from},"to":{to},...})
//   EMAIL_FROM      the sender address
// Any 2xx answer counts as sent.
int ms_email_enabled();
// 0 when the service accepted it
int ms_email_send(char const *to, char const *subject, char const *text);
