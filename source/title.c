/*
 * title.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * nxdumptool is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "utils.h"
#include "title.h"
#include "gamecard.h"

#define NS_APPLICATION_RECORD_LIMIT 4096

/* Global variables. */

static Mutex g_titleMutex = 0;
static bool g_titleInterfaceInit = false, g_titleGameCardAvailable = false;

static NsApplicationControlData *g_nsAppControlData = NULL;

static TitleApplicationMetadata *g_appMetadata = NULL;
static u32 g_appMetadataCount = 0;

static NcmContentMetaDatabase g_ncmDbGameCard = {0}, g_ncmDbEmmcSystem = {0}, g_ncmDbEmmcUser = {0}, g_ncmDbSdCard = {0};
static NcmContentStorage g_ncmStorageGameCard = {0}, g_ncmStorageEmmcSystem = {0}, g_ncmStorageEmmcUser = {0}, g_ncmStorageSdCard = {0};

static TitleInfo *g_titleInfo = NULL;
static u32 g_titleInfoCount = 0, g_titleInfoGameCardStartIndex = 0, g_titleInfoGameCardCount = 0;

/* Function prototypes. */

NX_INLINE void titleFreeApplicationMetadata(void);
NX_INLINE void titleFreeTitleInfo(void);

NX_INLINE TitleApplicationMetadata *titleFindApplicationMetadataByTitleId(u64 title_id);

static bool titleRetrieveApplicationMetadataFromNsRecords(void);
static bool titleRetrieveApplicationMetadataByTitleId(u64 title_id, TitleApplicationMetadata *out);

static bool titleOpenNcmDatabases(void);
static void titleCloseNcmDatabases(void);

static bool titleOpenNcmStorages(void);
static void titleCloseNcmStorages(void);

static bool titleOpenNcmDatabaseAndStorageFromGameCard(void);
static void titleCloseNcmDatabaseAndStorageFromGameCard(void);

static bool titleLoadTitleInfo(void);
static bool titleRetrieveContentMetaKeysFromDatabase(u8 storage_id);
static bool titleGetContentInfosFromTitle(u8 storage_id, const NcmContentMetaKey *meta_key, NcmContentInfo **out_content_infos, u32 *out_content_count);

static bool _titleRefreshGameCardTitleInfo(bool lock);
static void titleRemoveGameCardTitleInfoEntries(void);

bool titleInitialize(void)
{
    mutexLock(&g_titleMutex);
    
    bool ret = g_titleInterfaceInit;
    if (ret) goto end;
    
    /* Allocate memory for the ns application control data. */
    /* This will be used each time we need to retrieve the metadata from an application. */
    g_nsAppControlData = calloc(1, sizeof(NsApplicationControlData));
    if (!g_nsAppControlData)
    {
        LOGFILE("Failed to allocate memory for the ns application control data!");
        goto end;
    }
    
    /* Retrieve application metadata from ns records. */
    /* Theoretically speaking, we should only need to do this once. */
    /* However, if any new gamecard is inserted while the application is running, we *will* have to retrieve the metadata from its application(s). */
    if (!titleRetrieveApplicationMetadataFromNsRecords())
    {
        LOGFILE("Failed to retrieve application metadata from ns records!");
        goto end;
    }
    
    /* Open eMMC System, eMMC User and SD card ncm databases. */
    if (!titleOpenNcmDatabases())
    {
        LOGFILE("Failed to open ncm databases!");
        goto end;
    }
    
    /* Open eMMC System, eMMC User and SD card ncm storages. */
    if (!titleOpenNcmStorages())
    {
        LOGFILE("Failed to open ncm storages!");
        goto end;
    }
    
    /* Load title info by retrieving content meta keys from available eMMC System, eMMC User and SD card titles. */
    if (!titleLoadTitleInfo())
    {
        LOGFILE("Failed to load title info!");
        goto end;
    }
    
    /* Initial gamecard title info retrieval. */
    _titleRefreshGameCardTitleInfo(false);
    
    
    
    
    
    
    
    
    
    
    if (g_titleInfo && g_titleInfoCount)
    {
        mkdir("sdmc:/records", 0777);
        
        FILE *title_infos_txt = NULL, *icon_jpg = NULL;
        char icon_path[FS_MAX_PATH] = {0};
        
        title_infos_txt = fopen("sdmc:/records/title_infos.txt", "wb");
        if (title_infos_txt)
        {
            for(u32 i = 0; i < g_titleInfoCount; i++)
            {
                fprintf(title_infos_txt, "Storage ID: 0x%02X\r\n", g_titleInfo[i].storage_id);
                fprintf(title_infos_txt, "Title ID: %016lX\r\n", g_titleInfo[i].meta_key.id);
                fprintf(title_infos_txt, "Version: %u (%u.%u.%u-%u.%u)\r\n", g_titleInfo[i].meta_key.version, g_titleInfo[i].dot_version.TitleVersion_Major, \
                        g_titleInfo[i].dot_version.TitleVersion_Minor, g_titleInfo[i].dot_version.TitleVersion_Micro, g_titleInfo[i].dot_version.TitleVersion_MajorRelstep, \
                        g_titleInfo[i].dot_version.TitleVersion_MinorRelstep);
                fprintf(title_infos_txt, "Type: 0x%02X\r\n", g_titleInfo[i].meta_key.type);
                fprintf(title_infos_txt, "Install Type: 0x%02X\r\n", g_titleInfo[i].meta_key.install_type);
                fprintf(title_infos_txt, "Title Size: %s (0x%lX)\r\n", g_titleInfo[i].title_size_str, g_titleInfo[i].title_size);
                
                fprintf(title_infos_txt, "Content Count: %u\r\n", g_titleInfo[i].content_count);
                for(u32 j = 0; j < g_titleInfo[i].content_count; j++)
                {
                    char content_id_str[SHA256_HASH_SIZE + 1] = {0};
                    utilsGenerateHexStringFromData(content_id_str, sizeof(content_id_str), g_titleInfo[i].content_infos[j].content_id.c, SHA256_HASH_SIZE / 2);
                    
                    u64 content_size = 0;
                    titleConvertNcmContentSizeToU64(g_titleInfo[i].content_infos[j].size, &content_size);
                    
                    char content_size_str[32] = {0};
                    utilsGenerateFormattedSizeString(content_size, content_size_str, sizeof(content_size_str));
                    
                    fprintf(title_infos_txt, "    Content #%u:\r\n", j + 1);
                    fprintf(title_infos_txt, "        Content ID: %s\r\n", content_id_str);
                    fprintf(title_infos_txt, "        Content Size: %s (0x%lX)\r\n", content_size_str, content_size);
                    fprintf(title_infos_txt, "        Content Type: 0x%02X\r\n", g_titleInfo[i].content_infos[j].content_type);
                    fprintf(title_infos_txt, "        ID Offset: 0x%02X\r\n", g_titleInfo[i].content_infos[j].id_offset);
                }
                
                if (g_titleInfo[i].app_metadata)
                {
                    fprintf(title_infos_txt, "Application Name: %s\r\n", g_titleInfo[i].app_metadata->lang_entry.name);
                    fprintf(title_infos_txt, "Application Author: %s\r\n", g_titleInfo[i].app_metadata->lang_entry.author);
                    fprintf(title_infos_txt, "JPEG Icon Size: 0x%X\r\n", g_titleInfo[i].app_metadata->icon_size);
                    
                    if (g_titleInfo[i].app_metadata->icon_size)
                    {
                        sprintf(icon_path, "sdmc:/records/%016lX.jpg", g_titleInfo[i].app_metadata->title_id);
                        icon_jpg = fopen(icon_path, "wb");
                        if (icon_jpg)
                        {
                            fwrite(g_titleInfo[i].app_metadata->icon, 1, g_titleInfo[i].app_metadata->icon_size, icon_jpg);
                            fclose(icon_jpg);
                            icon_jpg = NULL;
                        }
                    }
                }
                
                fprintf(title_infos_txt, "\r\n");
                
                fflush(title_infos_txt);
            }
            
            fclose(title_infos_txt);
            title_infos_txt = NULL;
        }
    }
    
    
    
    
    
    
    
    
    
    
    
    
    ret = g_titleInterfaceInit = true;
    
end:
    mutexUnlock(&g_titleMutex);
    
    return ret;
}

void titleExit(void)
{
    mutexLock(&g_titleMutex);
    
    /* Free title info. */
    titleFreeTitleInfo();
    
    /* Close gamecard ncm database and storage. */
    titleCloseNcmDatabaseAndStorageFromGameCard();
    
    /* Close eMMC System, eMMC User and SD card ncm storages. */
    titleCloseNcmStorages();
    
    /* Close eMMC System, eMMC User and SD card ncm databases. */
    titleCloseNcmDatabases();
    
    /* Free application metadata. */
    titleFreeApplicationMetadata();
    
    /* Free ns application control data. */
    if (g_nsAppControlData) free(g_nsAppControlData);
    
    g_titleInterfaceInit = false;
    
    mutexUnlock(&g_titleMutex);
}

NcmContentMetaDatabase *titleGetNcmDatabaseByStorageId(u8 storage_id)
{
    NcmContentMetaDatabase *ncm_db = NULL;
    
    switch(storage_id)
    {
        case NcmStorageId_GameCard:
            ncm_db = &g_ncmDbGameCard;
            break;
        case NcmStorageId_BuiltInSystem:
            ncm_db = &g_ncmDbEmmcSystem;
            break;
        case NcmStorageId_BuiltInUser:
            ncm_db = &g_ncmDbEmmcUser;
            break;
        case NcmStorageId_SdCard:
            ncm_db = &g_ncmDbSdCard;
            break;
        default:
            break;
    }
    
    return ncm_db;
}

NcmContentStorage *titleGetNcmStorageByStorageId(u8 storage_id)
{
    NcmContentStorage *ncm_storage = NULL;
    
    switch(storage_id)
    {
        case NcmStorageId_GameCard:
            ncm_storage = &g_ncmStorageGameCard;
            break;
        case NcmStorageId_BuiltInSystem:
            ncm_storage = &g_ncmStorageEmmcSystem;
            break;
        case NcmStorageId_BuiltInUser:
            ncm_storage = &g_ncmStorageEmmcUser;
            break;
        case NcmStorageId_SdCard:
            ncm_storage = &g_ncmStorageSdCard;
            break;
        default:
            break;
    }
    
    return ncm_storage;
}

bool titleRefreshGameCardTitleInfo(void)
{
    return _titleRefreshGameCardTitleInfo(true);
}

TitleInfo *titleGetInfoFromStorageByTitleId(u8 storage_id, u64 title_id)
{
    mutexLock(&g_titleMutex);
    
    TitleInfo *info = NULL;
    
    if (!g_titleInfo || !g_titleInfoCount || storage_id < NcmStorageId_GameCard || storage_id > NcmStorageId_Any || !title_id)
    {
        LOGFILE("Invalid parameters!");
        goto end;
    }
    
    for(u32 i = 0; i < g_titleInfoCount; i++)
    {
        if (g_titleInfo[i].meta_key.id == title_id && (storage_id == NcmStorageId_Any || (storage_id != NcmStorageId_Any && g_titleInfo[i].storage_id == storage_id)))
        {
            info = &(g_titleInfo[i]);
            break;
        }
    }
    
    if (!info) LOGFILE("Unable to find TitleInfo entry with ID \"%016lX\"! (storage ID %u).", title_id, storage_id);
    
end:
    mutexUnlock(&g_titleMutex);
    
    return info;
}















NX_INLINE void titleFreeApplicationMetadata(void)
{
    if (g_appMetadata)
    {
        free(g_appMetadata);
        g_appMetadata = NULL;
    }
    
    g_appMetadataCount = 0;
}

NX_INLINE void titleFreeTitleInfo(void)
{
    if (g_titleInfo)
    {
        for(u32 i = 0; i < g_titleInfoCount; i++)
        {
            if (g_titleInfo[i].content_infos) free(g_titleInfo[i].content_infos);
        }
        
        free(g_titleInfo);
        g_titleInfo = NULL;
    }
    
    g_titleInfoCount = g_titleInfoGameCardStartIndex = g_titleInfoGameCardCount = 0;
}

NX_INLINE TitleApplicationMetadata *titleFindApplicationMetadataByTitleId(u64 title_id)
{
    if (!g_appMetadata || !g_appMetadataCount || !title_id) return NULL;
    
    for(u32 i = 0; i < g_appMetadataCount; i++)
    {
        if (g_appMetadata[i].title_id == title_id) return &(g_appMetadata[i]);
    }
    
    return NULL;
}

static bool titleRetrieveApplicationMetadataFromNsRecords(void)
{
    /* Return right away if application metadata has already been retrieved. */
    if (g_appMetadata || g_appMetadataCount) return true;
    
    Result rc = 0;
    
    NsApplicationRecord *app_records = NULL;
    u32 app_records_count = 0;
    
    bool success = false;
    
    /* Allocate memory for the ns application records. */
    app_records = calloc(NS_APPLICATION_RECORD_LIMIT, sizeof(NsApplicationRecord));
    if (!app_records)
    {
        LOGFILE("Failed to allocate memory for ns application records!");
        goto end;
    }
    
    /* Retrieve ns application records. */
    rc = nsListApplicationRecord(app_records, NS_APPLICATION_RECORD_LIMIT, 0, (s32*)&app_records_count);
    if (R_FAILED(rc))
    {
        LOGFILE("nsListApplicationRecord failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Return right away if no records were retrieved. */
    if (!app_records_count)
    {
        success = true;
        goto end;
    }
    
    /* Allocate memory for the application metadata. */
    g_appMetadata = calloc(app_records_count, sizeof(TitleApplicationMetadata));
    if (!g_appMetadata)
    {
        LOGFILE("Failed to allocate memory for application metadata! (%u %s).", app_records_count, app_records_count > 1 ? "entries" : "entry");
        goto end;
    }
    
    /* Retrieve application metadata for each ns application record. */
    g_appMetadataCount = 0;
    for(u32 i = 0; i < app_records_count; i++)
    {
        if (!titleRetrieveApplicationMetadataByTitleId(app_records[i].application_id, &(g_appMetadata[g_appMetadataCount]))) continue;
        g_appMetadataCount++;
    }
    
    /* Check retrieved application metadata count. */
    if (!g_appMetadataCount)
    {
        LOGFILE("Unable to retrieve application metadata from ns application records! (%u %s).", app_records_count, app_records_count > 1 ? "entries" : "entry");
        goto end;
    }
    
    /* Decrease application metadata buffer size if needed. */
    if (g_appMetadataCount < app_records_count)
    {
        TitleApplicationMetadata *tmp_app_metadata = realloc(g_appMetadata, g_appMetadataCount * sizeof(TitleApplicationMetadata));
        if (!tmp_app_metadata)
        {
            LOGFILE("Failed to reallocate application metadata buffer! (%u %s).", g_appMetadataCount, g_appMetadataCount > 1 ? "entries" : "entry");
            goto end;
        }
        
        g_appMetadata = tmp_app_metadata;
        tmp_app_metadata = NULL;
    }
    
    success = true;
    
end:
    if (!success)
    {
        if (g_appMetadata)
        {
            free(g_appMetadata);
            g_appMetadata = NULL;
        }
        
        g_appMetadataCount = 0;
    }
    
    if (app_records) free(app_records);
    
    return success;
}

static bool titleRetrieveApplicationMetadataByTitleId(u64 title_id, TitleApplicationMetadata *out)
{
    if (!g_nsAppControlData || !title_id || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u64 write_size = 0;
    NacpLanguageEntry *lang_entry = NULL;
    
    /* Retrieve ns application control data. */
    rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, title_id, g_nsAppControlData, sizeof(NsApplicationControlData), &write_size);
    if (R_FAILED(rc))
    {
        LOGFILE("nsGetApplicationControlData failed for title ID \"%016lX\"! (0x%08X).", rc, title_id);
        return false;
    }
    
    if (write_size < sizeof(NacpStruct))
    {
        LOGFILE("Retrieved application control data buffer is too small! (0x%lX).", write_size);
        return false;
    }
    
    /* Get language entry. */
    rc = nacpGetLanguageEntry(&(g_nsAppControlData->nacp), &lang_entry);
    if (R_FAILED(rc))
    {
        LOGFILE("nacpGetLanguageEntry failed! (0x%08X).", rc);
        return false;
    }
    
    /* Copy data. */
    out->title_id = title_id;
    
    memcpy(&(out->lang_entry), lang_entry, sizeof(NacpLanguageEntry));
    utilsTrimString(out->lang_entry.name);
    utilsTrimString(out->lang_entry.author);
    
    out->icon_size = (write_size - sizeof(NacpStruct));
    memcpy(out->icon, g_nsAppControlData->icon, sizeof(g_nsAppControlData->icon));
    
    return true;
}

static bool titleOpenNcmDatabases(void)
{
    Result rc = 0;
    NcmContentMetaDatabase *ncm_db = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm database pointer. */
        ncm_db = titleGetNcmDatabaseByStorageId(i);
        if (!ncm_db)
        {
            LOGFILE("Failed to retrieve ncm database pointer for storage ID %u!", i);
            return false;
        }
        
        /* Check if the ncm database handle has already been retrieved. */
        if (serviceIsActive(&(ncm_db->s))) continue;
        
        /* Open ncm database. */
        rc = ncmOpenContentMetaDatabase(ncm_db, i);
        if (R_FAILED(rc))
        {
            /* If the SD card is mounted, but it isn't currently being used by HOS, 0x21005 will be returned, so we'll just filter this particular error and continue. */
            /* This can occur when using the "Nintendo" directory from a different console, or when the "sdmc:/Nintendo/Contents/private" file is corrupted. */
            LOGFILE("ncmOpenContentMetaDatabase failed for storage ID %u! (0x%08X).", i, rc);
            if (i == NcmStorageId_SdCard && rc == 0x21005) continue;
            return false;
        }
    }
    
    return true;
}

static void titleCloseNcmDatabases(void)
{
    NcmContentMetaDatabase *ncm_db = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm database pointer. */
        ncm_db = titleGetNcmDatabaseByStorageId(i);
        if (!ncm_db) continue;
        
        /* Check if the ncm database handle has already been retrieved. */
        if (serviceIsActive(&(ncm_db->s))) ncmContentMetaDatabaseClose(ncm_db);
    }
}

static bool titleOpenNcmStorages(void)
{
    Result rc = 0;
    NcmContentStorage *ncm_storage = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm storage pointer. */
        ncm_storage = titleGetNcmStorageByStorageId(i);
        if (!ncm_storage)
        {
            LOGFILE("Failed to retrieve ncm storage pointer for storage ID %u!", i);
            return false;
        }
        
        /* Check if the ncm storage handle has already been retrieved. */
        if (serviceIsActive(&(ncm_storage->s))) continue;
        
        /* Open ncm storage. */
        rc = ncmOpenContentStorage(ncm_storage, i);
        if (R_FAILED(rc))
        {
            /* If the SD card is mounted, but it isn't currently being used by HOS, 0x21005 will be returned, so we'll just filter this particular error and continue. */
            /* This can occur when using the "Nintendo" directory from a different console, or when the "sdmc:/Nintendo/Contents/private" file is corrupted. */
            LOGFILE("ncmOpenContentStorage failed for storage ID %u! (0x%08X).", i, rc);
            if (i == NcmStorageId_SdCard && rc == 0x21005) continue;
            return false;
        }
    }
    
    return true;
}

static void titleCloseNcmStorages(void)
{
    NcmContentStorage *ncm_storage = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm storage pointer. */
        ncm_storage = titleGetNcmStorageByStorageId(i);
        if (!ncm_storage) continue;
        
        /* Check if the ncm storage handle has already been retrieved. */
        if (serviceIsActive(&(ncm_storage->s))) ncmContentStorageClose(ncm_storage);
    }
}

static bool titleOpenNcmDatabaseAndStorageFromGameCard(void)
{
    Result rc = 0;
    NcmContentMetaDatabase *ncm_db = &g_ncmDbGameCard;
    NcmContentStorage *ncm_storage = &g_ncmStorageGameCard;
    
    /* Open ncm database. */
    rc = ncmOpenContentMetaDatabase(ncm_db, NcmStorageId_GameCard);
    if (R_FAILED(rc))
    {
        LOGFILE("ncmOpenContentMetaDatabase failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Open ncm storage. */
    rc = ncmOpenContentStorage(ncm_storage, NcmStorageId_GameCard);
    if (R_FAILED(rc))
    {
        LOGFILE("ncmOpenContentStorage failed! (0x%08X).", rc);
        goto end;
    }
    
end:
    return R_SUCCEEDED(rc);
}

static void titleCloseNcmDatabaseAndStorageFromGameCard(void)
{
    NcmContentMetaDatabase *ncm_db = &g_ncmDbGameCard;
    NcmContentStorage *ncm_storage = &g_ncmStorageGameCard;
    
    /* Check if the ncm database handle has already been retrieved. */
    if (serviceIsActive(&(ncm_db->s))) ncmContentMetaDatabaseClose(ncm_db);
    
    /* Check if the ncm storage handle has already been retrieved. */
    if (serviceIsActive(&(ncm_storage->s))) ncmContentStorageClose(ncm_storage);
}

static bool titleLoadTitleInfo(void)
{
    /* Return right away if title info has already been retrieved. */
    if (g_titleInfo || g_titleInfoCount) return true;
    
    g_titleInfoCount = 0;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve content meta keys from the current storage. */
        if (!titleRetrieveContentMetaKeysFromDatabase(i))
        {
            LOGFILE("Failed to retrieve content meta keys from storage ID %u!", i);
            return false;
        }
    }
    
    return true;
}

static bool titleRetrieveContentMetaKeysFromDatabase(u8 storage_id)
{
    NcmContentMetaDatabase *ncm_db = NULL;
    
    if (!(ncm_db = titleGetNcmDatabaseByStorageId(storage_id)) || !serviceIsActive(&(ncm_db->s)))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    
    u32 written = 0, total = 0;
    NcmContentMetaKey *meta_keys = NULL, *meta_keys_tmp = NULL;
    size_t meta_keys_size = sizeof(NcmContentMetaKey);
    
    TitleInfo *tmp_title_info = NULL;
    
    bool success = false;
    
    /* Allocate memory for the ncm application content meta keys. */
    meta_keys = calloc(1, meta_keys_size);
    if (!meta_keys)
    {
        LOGFILE("Unable to allocate memory for the ncm application meta keys!");
        goto end;
    }
    
    /* Get a full list of all titles available in this storage. */
    /* Meta type '0' means all title types will be retrieved. */
    rc = ncmContentMetaDatabaseList(ncm_db, (s32*)&total, (s32*)&written, meta_keys, 1, 0, 0, 0, -1, NcmContentInstallType_Full);
    if (R_FAILED(rc))
    {
        LOGFILE("ncmContentMetaDatabaseList failed! (0x%08X) (first entry).", rc);
        goto end;
    }
    
    /* Check if our application meta keys buffer was actually filled. */
    /* If it wasn't, odds are there are no titles in this storage. */
    if (!written || !total)
    {
        success = true;
        goto end;
    }
    
    /* Check if we need to resize our application meta keys buffer. */
    if (total > written)
    {
        /* Update application meta keys buffer size. */
        meta_keys_size *= total;
        
        /* Reallocate application meta keys buffer. */
        meta_keys_tmp = realloc(meta_keys, meta_keys_size);
        if (!meta_keys_tmp)
        {
            LOGFILE("Unable to reallocate application meta keys buffer! (%u entries).", total);
            goto end;
        }
        
        meta_keys = meta_keys_tmp;
        meta_keys_tmp = NULL;
        
        /* Issue call again. */
        rc = ncmContentMetaDatabaseList(ncm_db, (s32*)&total, (s32*)&written, meta_keys, (s32)total, 0, 0, 0, -1, NcmContentInstallType_Full);
        if (R_FAILED(rc))
        {
            LOGFILE("ncmContentMetaDatabaseList failed! (0x%08X) (%u %s).", rc, total, total > 1 ? "entries" : "entry");
            goto end;
        }
        
        /* Safety check. */
        if (written != total)
        {
            LOGFILE("Application meta key count mismatch! (%u != %u).", written, total);
            goto end;
        }
    }
    
    /* Reallocate title info buffer. */
    /* If g_titleInfo == NULL, realloc() will essentially act as a malloc(). */
    tmp_title_info = realloc(g_titleInfo, (g_titleInfoCount + total) * sizeof(TitleInfo));
    if (!tmp_title_info)
    {
        LOGFILE("Unable to reallocate title info buffer! (%u %s).", g_titleInfoCount + total, (g_titleInfoCount + total) > 1 ? "entries" : "entry");
        goto end;
    }
    
    g_titleInfo = tmp_title_info;
    tmp_title_info = NULL;
    
    /* Clear new title info buffer area. */
    memset(g_titleInfo + g_titleInfoCount, 0, total * sizeof(TitleInfo));
    
    /* Fill new title info entries. */
    for(u32 i = 0; i < total; i++)
    {
        TitleInfo *cur_title_info = &(g_titleInfo[g_titleInfoCount + i]);
        
        /* Fill information. */
        cur_title_info->storage_id = storage_id;
        memcpy(&(cur_title_info->dot_version), &(meta_keys[i].version), sizeof(u32));
        memcpy(&(cur_title_info->meta_key), &(meta_keys[i]), sizeof(NcmContentMetaKey));
        cur_title_info->app_metadata = titleFindApplicationMetadataByTitleId(meta_keys[i].id);
        
        /* Retrieve content infos. */
        if (titleGetContentInfosFromTitle(storage_id, &(meta_keys[i]), &(cur_title_info->content_infos), &(cur_title_info->content_count)))
        {
            /* Calculate title size. */
            for(u32 j = 0; j < cur_title_info->content_count; j++)
            {
                u64 tmp_size = 0;
                titleConvertNcmContentSizeToU64(cur_title_info->content_infos[j].size, &tmp_size);
                cur_title_info->title_size += tmp_size;
            }
        }
        
        /* Generate formatted title size string. */
        utilsGenerateFormattedSizeString(cur_title_info->title_size, cur_title_info->title_size_str, sizeof(cur_title_info->title_size_str));
    }
    
    /* Update title info count. */
    g_titleInfoCount += total;
    
    success = true;
    
end:
    if (meta_keys) free(meta_keys);
    
    return success;
}

static bool titleGetContentInfosFromTitle(u8 storage_id, const NcmContentMetaKey *meta_key, NcmContentInfo **out_content_infos, u32 *out_content_count)
{
    NcmContentMetaDatabase *ncm_db = NULL;
    
    if (!(ncm_db = titleGetNcmDatabaseByStorageId(storage_id)) || !serviceIsActive(&(ncm_db->s)) || !meta_key || !out_content_infos || !out_content_count)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    
    NcmContentMetaHeader content_meta_header = {0};
    u64 content_meta_header_read_size = 0;
    
    NcmContentInfo *content_infos = NULL;
    u32 content_count = 0, written = 0;
    
    bool success = false;
    
    /* Retrieve content meta header. */
    rc = ncmContentMetaDatabaseGet(ncm_db, meta_key, &content_meta_header_read_size, &content_meta_header, sizeof(NcmContentMetaHeader));
    if (R_FAILED(rc))
    {
        LOGFILE("ncmContentMetaDatabaseGet failed! (0x%08X).", rc);
        goto end;
    }
    
    if (content_meta_header_read_size != sizeof(NcmContentMetaHeader))
    {
        LOGFILE("Content meta header size mismatch! (0x%lX != 0x%lX).", rc, content_meta_header_read_size, sizeof(NcmContentMetaHeader));
        goto end;
    }
    
    /* Get content count. */
    content_count = (u32)content_meta_header.content_count;
    if (!content_count)
    {
        LOGFILE("Content count is zero!");
        goto end;
    }
    
    /* Allocate memory for the content infos. */
    content_infos = calloc(content_count, sizeof(NcmContentInfo));
    if (!content_infos)
    {
        LOGFILE("Unable to allocate memory for the content infos buffer! (%u content[s]).", content_count);
        goto end;
    }
    
    /* Retrieve content infos. */
    rc = ncmContentMetaDatabaseListContentInfo(ncm_db, (s32*)&written, content_infos, (s32)content_count, meta_key, 0);
    if (R_FAILED(rc))
    {
        LOGFILE("ncmContentMetaDatabaseListContentInfo failed! (0x%08X).", rc);
        goto end;
    }
    
    if (written != content_count)
    {
        LOGFILE("Content count mismatch! (%u != %u).", written, content_count);
        goto end;
    }
    
    /* Update output. */
    *out_content_infos = content_infos;
    *out_content_count = content_count;
    
    success = true;
    
end:
    if (!success && content_infos) free(content_infos);
    
    return success;
}

static bool _titleRefreshGameCardTitleInfo(bool lock)
{
    if (lock) mutexLock(&g_titleMutex);
    
    TitleApplicationMetadata *tmp_app_metadata = NULL;
    u32 orig_app_count = g_appMetadataCount, cur_app_count = g_appMetadataCount, gamecard_app_count = 0, gamecard_metadata_count = 0;
    bool status = false, success = false, cleanup = true;
    
    /* Retrieve current gamecard status. */
    status = (gamecardGetStatus() == GameCardStatus_InsertedAndInfoLoaded);
    if (status == g_titleGameCardAvailable || !status)
    {
        cleanup = (status != g_titleGameCardAvailable);
        goto end;
    }
    
    /* Open gamecard ncm database and storage handles. */
    if (!titleOpenNcmDatabaseAndStorageFromGameCard())
    {
        LOGFILE("Failed to open gamecard ncm database and storage handles.");
        goto end;
    }
    
    /* Update start index for the gamecard title info entries. */
    g_titleInfoGameCardStartIndex = g_titleInfoCount;
    
    /* Retrieve content meta keys from the gamecard ncm database. */
    if (!titleRetrieveContentMetaKeysFromDatabase(NcmStorageId_GameCard))
    {
        LOGFILE("Failed to retrieve content meta keys from gamecard!");
        goto end;
    }
    
    /* Update gamecard title info count. */
    g_titleInfoGameCardCount = (g_titleInfoCount - g_titleInfoGameCardStartIndex);
    if (!g_titleInfoGameCardCount)
    {
        LOGFILE("Empty content meta key count from gamecard!");
        goto end;
    }
    
    /* Retrieve gamecard application metadata. */
    for(u32 i = g_titleInfoGameCardStartIndex; i < g_titleInfoCount; i++)
    {
        TitleInfo *cur_title_info = &(g_titleInfo[i]);
        
        /* Skip current title if it's not an application. */
        if (cur_title_info->meta_key.type != NcmContentMetaType_Application) continue;
        gamecard_app_count++;
        
        /* Check if we already have an application metadata entry for this title ID. */
        if ((cur_title_info->app_metadata = titleFindApplicationMetadataByTitleId(cur_title_info->meta_key.id)) != NULL)
        {
            gamecard_metadata_count++;
            continue;
        }
        
        /* Reallocate application metadata buffer (if needed). */
        if (cur_app_count < (g_appMetadataCount + 1))
        {
            tmp_app_metadata = realloc(g_appMetadata, (g_appMetadataCount + 1) * sizeof(TitleApplicationMetadata));
            if (!tmp_app_metadata)
            {
                LOGFILE("Failed to reallocate application metadata buffer! (additional entry).");
                goto end;
            }
            
            g_appMetadata = tmp_app_metadata;
            tmp_app_metadata = NULL;
            
            cur_app_count++;
        }
        
        /* Retrieve application metadata. */
        if (!titleRetrieveApplicationMetadataByTitleId(cur_title_info->meta_key.id, &(g_appMetadata[g_appMetadataCount]))) continue;
        
        cur_title_info->app_metadata = &(g_appMetadata[g_appMetadataCount]);
        g_appMetadataCount++;
        
        gamecard_metadata_count++;
    }
    
    /* Check gamecard application count. */
    if (!gamecard_app_count)
    {
        LOGFILE("Gamecard application count is zero!");
        goto end;
    }
    
    /* Check retrieved application metadata count. */
    if (!gamecard_metadata_count)
    {
        LOGFILE("Unable to retrieve application metadata from gamecard! (%u %s).", gamecard_app_count, gamecard_app_count > 1 ? "entries" : "entry");
        goto end;
    }
    
    success = true;
    cleanup = false;
    
end:
    /* Update gamecard status. */
    g_titleGameCardAvailable = status;
    
    /* Decrease application metadata buffer size if needed. */
    if ((success && g_appMetadataCount < cur_app_count) || (!success && g_appMetadataCount > orig_app_count))
    {
        if (!success) g_appMetadataCount = orig_app_count;
        
        tmp_app_metadata = realloc(g_appMetadata, g_appMetadataCount * sizeof(TitleApplicationMetadata));
        if (tmp_app_metadata)
        {
            g_appMetadata = tmp_app_metadata;
            tmp_app_metadata = NULL;
        }
    }
    
    if (cleanup)
    {
        titleRemoveGameCardTitleInfoEntries();
        titleCloseNcmDatabaseAndStorageFromGameCard();
    }
    
    if (lock) mutexUnlock(&g_titleMutex);
    
    return success;
}

static void titleRemoveGameCardTitleInfoEntries(void)
{
    if (!g_titleInfo || !g_titleInfoCount || !g_titleInfoGameCardCount || g_titleInfoGameCardCount > g_titleInfoCount || \
        g_titleInfoGameCardStartIndex != (g_titleInfoCount - g_titleInfoGameCardCount)) return;
    
    if (g_titleInfoGameCardCount == g_titleInfoCount)
    {
        titleFreeTitleInfo();
    } else {
        for(u32 i = (g_titleInfoCount - g_titleInfoGameCardCount); i < g_titleInfoCount; i++)
        {
            if (g_titleInfo[i].content_infos) free(g_titleInfo[i].content_infos);
        }
        
        TitleInfo *tmp_title_info = realloc(g_titleInfo, (g_titleInfoCount - g_titleInfoGameCardCount) * sizeof(TitleInfo));
        if (tmp_title_info)
        {
            g_titleInfo = tmp_title_info;
            tmp_title_info = NULL;
        }
        
        g_titleInfoCount = (g_titleInfoCount - g_titleInfoGameCardCount);
        g_titleInfoGameCardStartIndex = g_titleInfoGameCardCount = 0;
    }
}
