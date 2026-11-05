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

// Outgoing email through Resend's events API (https://resend.com/docs/api-reference/events/send-event).
// Blocking: call it from a worker thread. It needs, in the master's env:
//   RESEND_API_KEY  the key (Authorization: Bearer ...); it only ever goes to api.resend.com
//   RESEND_EVENT    the name of the event the automation in Resend listens to (default "OTP SEND")
// Each mail is POST /events/send {"event":..,"email":to,"payload":{"subject":..,"text":..,"OTP":..}};
// the sender and the layout belong to the automation in the Resend dashboard.
// Any 2xx answer counts as sent. At most EMAIL_DAILY_LIMIT (default 50, 0 = no limit)
// emails go out per UTC day, counted since the master started.
int ms_email_enabled();
// A mail to ADMIN_EMAIL that goes out even when today's emails are used up (something that
// needs a human, like a notice from Apple). At most 30 an hour. 0 when sent
int ms_email_send_admin(char const *subject, char const *text);
// 1 when today's emails are used up
int ms_email_daily_full();
// `otp` is the one-time code in `text` (NULL when there is none). 0 when the service
// accepted it, -2 when the daily limit is used up
int ms_email_send(char const *to, char const *subject, char const *text,
                  char const *otp);
