#include "patches.h"

uint32_t doBL(uint32_t dst, uint32_t src)
{
	uint32_t newval = (dst - src);
	newval &= 0x03FFFFFC;
	newval |= 0x48000001;
	return newval;
}

void Write_U32(uint32_t addr, uint32_t value)
{
	KernelCopyData(OSEffectiveToPhysical(addr), OSEffectiveToPhysical((uint32_t)&value), sizeof(value));
	// Only works on AROMA! WUPS 0.1's KernelCopyData is uncached, needs DCInvalidate here instead
	DCFlushRange((void *)addr, sizeof(value));
	ICInvalidateRange((void *)addr, sizeof(value));
}

// ==========================================================================================

DECL_FUNCTION(bool, enl_ParseIdentificationToken, void *identifiationInfo, sead_String *identificationToken)
{

	// Fix for RCE (stack overflow if identification buffer was bigger than 16)
	if (strnlen(identificationToken->mBuffer, 16) == 16)
	{
		identificationToken->mBuffer[15] = '\0';
		return real_enl_ParseIdentificationToken(identifiationInfo, identificationToken);
	}

	return real_enl_ParseIdentificationToken(identifiationInfo, identificationToken);
}

enl_ContentTransporter *(*real_enl_TransportManager_getContentTransporter)(void *_this, unsigned char &id);
DECL_FUNCTION(void, enl_TransportManager_updateReceiveBuffer_, void *_this, signed char const &bufferId, uint8_t *data, uint32_t size)
{

	// Loop through all records and check if there's a bad record (size mismatch) until out of bounds or end record
	uint8_t *pData = data;
	while (pData < (data + size))
	{
		enl_RecordHeader *record = (enl_RecordHeader *)pData;
		if (record->mContentLength == 0 && record->mContentTransporterID == 0xff)
			break;

		enl_ContentTransporter *contentTransp = real_enl_TransportManager_getContentTransporter(_this, record->mContentTransporterID);
		// Actual fix for the ENL nullptr deref crash (lmao)
		if (!contentTransp)
			return;

		// Fix for RCE (if size mismatch, do not handle packet.)
		if (contentTransp->vtable->getSendBufferSize(contentTransp) != record->mContentLength)
			return;

		pData += sizeof(enl_RecordHeader);
		pData += record->mContentLength;
	}

	return real_enl_TransportManager_updateReceiveBuffer_(_this, bufferId, data, size);
}

void MARIO_KART_8_ApplyPatch(EPatchType type)
{
	auto turbo_rpx = FindRPL(*gRPLInfo, "Turbo.rpx");
	if (!turbo_rpx)
	{
		WHBLogPrintf("rce_patches: Couldn't find Turbo.rpx ...");
		return;
	}

	if (type == PATCH_ENL_ID_TOKEN_RCE)
	{
		// Address of 'enl::PiaUtil::ParseIdentificationToken'
		uint32_t addr_func = turbo_rpx->textAddr + 0x8E3930;
		function_replacement_data_t repl = REPLACE_FUNCTION_VIA_ADDRESS_FOR_PROCESS(
			enl_ParseIdentificationToken,
			OSEffectiveToPhysical(addr_func),
			addr_func,
			FP_TARGET_PROCESS_GAME_AND_MENU);
		FunctionPatcherPatchFunction(&repl, nullptr);

		WHBLogPrintf("rce_patches: Patched Mario Kart 8 (PATCH_ENL_ID_TOKEN_RCE)");
	}

	if (type == PATCH_ENL_BUFFER_RCE)
	{
		real_enl_TransportManager_getContentTransporter = (enl_ContentTransporter * (*)(void *, unsigned char &))(turbo_rpx->textAddr + 0x8D7678);

		// Address of 'enl::TransportManager::updateReceiveBuffer_'
		uint32_t addr_func = turbo_rpx->textAddr + 0x8D772C;
		function_replacement_data_t repl = REPLACE_FUNCTION_VIA_ADDRESS_FOR_PROCESS(
			enl_TransportManager_updateReceiveBuffer_,
			OSEffectiveToPhysical(addr_func),
			addr_func,
			FP_TARGET_PROCESS_GAME_AND_MENU);
		FunctionPatcherPatchFunction(&repl, nullptr);

		WHBLogPrintf("rce_patches: Patched Mario Kart 8 (PATCH_ENL_BUFFER_RCE)");
	}
}

// ==========================================================================================

void SPLATOON_ApplyPatch(EPatchType) {}