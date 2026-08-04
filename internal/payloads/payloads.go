// Package payloads embeds the ayame DLLs into the ayame CLI so a single
// executable can install the fix anywhere.
package payloads

import _ "embed"

//go:embed winmm.dll
var Winmm []byte

//go:embed EOSSDK-Win64-Shipping.dll
var EosProxy []byte
