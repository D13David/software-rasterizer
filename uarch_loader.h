#ifndef PJD_UARCH_LOADER_H
#define PJD_UARCH_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Archive* ArchiveHandle;

ArchiveHandle OpenArchive(const char* filename);
void CloseArchive(ArchiveHandle handle);
const struct ExportEntry* FindExportByName(ArchiveHandle archive, const char* name, const char* className);

// class loaders
int LoadMeshFromArchive(ArchiveHandle archive, const char* name, struct Mesh** outMesh);

#ifdef __cplusplus
}
#endif

#endif // PJD_UARCH_LOADER_H