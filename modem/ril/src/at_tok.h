/* SPDX-License-Identifier: Apache-2.0 */
/* //device/system/reference-ril/at_tok.h
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/
/*
 * mind_one note (RIL-MINIMAL-1409): copied verbatim, unmodified, from
 * hardware/ril/reference-ril/at_tok.{h,c} present read-only in the LineageOS tree
 * (hardware/ril/reference-ril/). This is a tiny, self-contained AT-response
 * tokenizer with no project-specific assumptions -- there is nothing to adapt. Apache-2.0,
 * original AOSP copyright preserved above.
 */
#ifndef AT_TOK_H
#define AT_TOK_H 1

#ifdef __cplusplus
extern "C" {
#endif

int at_tok_start(char **p_cur);
int at_tok_nextint(char **p_cur, int *p_out);
int at_tok_nexthexint(char **p_cur, int *p_out);

int at_tok_nextbool(char **p_cur, char *p_out);
int at_tok_nextstr(char **p_cur, char **out);

int at_tok_hasmore(char **p_cur);

#ifdef __cplusplus
}
#endif

#endif /*AT_TOK_H */
