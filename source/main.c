#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <3ds.h>

#define INSPECT_LOG_BUF_SIZE 200
// this is because SecureInfo_* lets it go up to 15 bytes
#define SERIAL_NUMBER_MAX 16

struct SecureInfo {
	char signature[0x100];
	uint8_t region;
	uint8_t padding;
	char serial[0xF];
} __attribute__((__packed__));

enum AppState {
	ROS_Started,
	ROS_Intro,
	ROS_ErrorWaitingExit,
	ROS_WaitingExit,
	ROS_WaitingRebootExit,
	ROS_CheckFiles,
	ROS_RestoreFiles,
	ROS_Exiting,
	ROS_Rebooting
};

struct AppStateData {
	enum AppState state;
	bool archives_mounted;
	char log_serial_number[SERIAL_NUMBER_MAX];
	char secureinfo_ab_letter;
	uint8_t target_region;
	char secureinfo_ab_path[26];
	struct SecureInfo secureinfo_ab;
	struct SecureInfo secureinfo_c;
	struct SecureInfo secureinfo_ab_replacement;
	bool secureinfo_c_loaded;
	PrintConsole top;
	PrintConsole bottom;
	int randval;
};

char regions[7][4] = {"JPN", "USA", "EUR", "AUS", "CHN", "KOR", "TWN"};

struct AppStateData data;

int logbt(const char* restrict format, ...) {
	int res;
	va_list args;
	va_start(args, format);

	consoleSelect(&data.bottom);
	res = vprintf(format, args);
	consoleSelect(&data.top);
	va_end(args);
	return res;
};

size_t get_serial_number_from_inspect_log(char *serial_out, char *inspect_log) {
	char *serial_offset = NULL;
	char *serial_value_offset = NULL;
	char *newline_offset = NULL;
	size_t serial_number_length = 0;

	serial_offset = strstr(inspect_log, "SerialNumber=");
	//printf("the SerialNumber= offset is %p\n", serial_offset);

	serial_value_offset = strstr(serial_offset, "=") + 1;
	//printf("the sn value offset is %p\n", serial_value_offset);

	newline_offset = strstr(serial_offset, "\n");

	serial_number_length = (size_t)(newline_offset - serial_value_offset);
	//printf("the sn is %zu bytes long\n", serial_number_length);
	
	strlcpy(serial_out, serial_value_offset, serial_number_length + 1);

	return serial_number_length;
}

void print_header(void) {
	consoleClear();
	printf("restore-overwritten-secureinfo by ihaveahax\n");
	printf("\n");
}

void print_warning(void) {
	/*      -------------------------------------------------- */
	printf("==================================================");
	printf("                    WARNING!!!                    ");
	printf("==================================================");
	printf("\n");
	printf("This is a SPECIALIZED tool for SPECIAL situations.");
	printf("If you were not instructuted to use this to repair");
	printf("your console, STOP!!! Turn back now!\n");
	printf("\n");
	printf("--------------------------------------------------");
	printf("\n");
}

void print_info(void) {
	/*      -------------------------------------------------- */
	printf("This tool will restore an overwritten SecureInfo_*");
	printf("file, using the serial number taken from\n");
	printf("inspect.log in TWLNAND.\n");
	printf("\n");
}

FILE *fopen_log(const char *restrict filename, const char *restrict mode) {
	logbt("fopen: %s %s\n", filename, mode);
	return fopen(filename, mode);
}

int rename_log(const char* old_filename, const char* new_filename) {
	logbt("rename:\n old: %s\n new: %s\n", old_filename, new_filename);
	return rename(old_filename, new_filename);
}

bool get_log_serial_number(char *out) {
	char inspect_log[INSPECT_LOG_BUF_SIZE] = {0};
	FILE *f;

	memset(out, 0, SERIAL_NUMBER_MAX);
	
	f = fopen_log("twln:/sys/log/inspect.log", "rb");
	if (!f) {
		printf("err: %s\n", strerror(errno));
		return false;
	}

	fread(inspect_log, sizeof(char), INSPECT_LOG_BUF_SIZE, f);
	fclose(f);

	get_serial_number_from_inspect_log(out, inspect_log);

	return true;
}

bool read_secureinfo(char letter, struct SecureInfo *out) {
	FILE *f;

	char path[26] = {0};
	snprintf(path, 26, "ctrn:/rw/sys/SecureInfo_%c", letter);

	if (letter == 'A' || letter == 'B') {
		strncpy(data.secureinfo_ab_path, path, 26);
	}

	f = fopen_log(path, "rb");
	if (!f) {
		return false;
	}
	
	//ab = 'A';
	//f = fopen("ctrn:/rw/sys/SecureInfo_A", "rb");
	//if (!f) {
	//	ab = 'B';
	//	f = fopen("ctrn:/rw/sys/SecureInfo_B", "rb");
	//	if (!f) {
	//		printf("err: %s\n", strerror(errno));
	//		/*      -------------------------------------------------- */
	//		printf("Could not open SecureInfo_A or SecureInfo_B.\n");
	//		printf("This should be impossible!!!\n");
	//		return 0;
	//	}
	//}

	fread(out, sizeof(struct SecureInfo), 1, f);
	fclose(f);

	return true;
}

bool mount_archives(void) {
	Result res;

	res = archiveMount(ARCHIVE_NAND_TWL_FS, fsMakePath(PATH_EMPTY, ""), "twln");
	if (R_FAILED(res)) {
		printf("archiveMount twln: 0x%08lx\n", res);
		return false;
	}
	logbt("Mounted twln:/\n");

	res = archiveMount(ARCHIVE_NAND_CTR_FS, fsMakePath(PATH_EMPTY, ""), "ctrn");
	if (R_FAILED(res)) {
		printf("archiveMount ctrn: 0x%08lx\n", res);
		archiveUnmount("twln");
		return false;
	}
	logbt("Mounted ctrn:/\n");

	return true;
}

void unmount_archives(void) {
	archiveUnmount("ctrn");
	logbt("Unmounted ctrn:/\n");
	archiveUnmount("twln");
	logbt("Unmounted twln:/\n");
}

bool is_serial_number_valid(char *serial) {
	// this makes the assumption that garbage doesn't somehow become these letters
	// but that's probably okay...
	switch(serial[0]) {
		case 'C': // Old 3DS
		case 'S': // Old 3DS XL/LL
		case 'A': // Old 2DS
		case 'Y': // New 3DS
		case 'Q': // New 3DS XL/LL
		case 'N': // New 2DS XL/LL
		case 'E': // Dev unit
		case 'R': // Dev unit
			return true;
		default:
			return false;
	}
}

bool backup_old_secureinfo(void) {
	int res;
	//char secinfo_ab_path[26] = {0};
	char secinfo_ab_new_path[36] = {0};
	char *secinfo_c_path = "ctrn:/rw/sys/SecureInfo_C";
	char secinfo_c_new_path[36] = {0};

	//snprintf(secinfo_ab_path, 26, "ctrn:/rw/sys/SecureInfo_%c", data.secureinfo_ab_letter);
	snprintf(secinfo_ab_new_path, 36, "%s.bak-%03i", data.secureinfo_ab_path, data.randval);
	res = rename_log(data.secureinfo_ab_path, secinfo_ab_new_path);
	if (res) {
		printf("Error on renaming: %s\n", strerror(errno));
		return false;
	}

	if (data.secureinfo_c_loaded) {
		snprintf(secinfo_c_new_path, 36, "%s.bak-%03i", secinfo_c_path, data.randval);
		res = rename_log(secinfo_c_path, secinfo_c_new_path);
		if (res) {
			printf("Error on renaming: %s\n", strerror(errno));
			return false;
		}
	}
	return true;
}

bool write_new_secureinfo_ab(void) {
	FILE *f;
	time_t rawtime;
	struct tm *timeinfo;

	f = fopen_log(data.secureinfo_ab_path, "wb");
	if (!f) {
		printf("Error opening: %s\n", strerror(errno));
		return false;
	}

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	snprintf(data.secureinfo_ab_replacement.signature, 0x100, "restored by https://github.com/ihaveamac/restore-overwritten-secureinfo at %s", asctime(timeinfo));

	fwrite(&data.secureinfo_ab_replacement, sizeof(struct SecureInfo), 1, f);
	fclose(f);
	return true;
}

enum AppState update_state(enum AppState wanted, u32 kDown) {
	bool changing = false;
	enum AppState current = data.state;
	enum AppState final = wanted;

	if (wanted != current) {
		changing = true;
	}
	switch(wanted) {
		case ROS_Started:
			final = update_state(ROS_Intro, 0);
			break;
		case ROS_Intro:
			if (changing) {
				print_header();
				print_warning();
				print_info();
				printf("Press START or B to exit.\n");
				printf("Press A to continue.\n");
			}

			if (kDown & KEY_START || kDown & KEY_B) {
				final = update_state(ROS_Exiting, 0);
			} else if (kDown & KEY_A) {
				final = update_state(ROS_CheckFiles, 0);
			}
			break;
		case ROS_ErrorWaitingExit:
			if (changing) {
				/*      -------------------------------------------------- */
				printf("\nA fatal error has happened.\n");
				printf("Please ask on Nintendo Homebrew for assistance.\n");
				printf("\nPress START or B to exit.");
			}

			if (kDown & KEY_START || kDown & KEY_B) {
				final = update_state(ROS_Exiting, 0);
			}
			break;
		case ROS_WaitingExit:
			if (changing) {
				printf("\nPress START or B to exit.");
			}

			if (kDown & KEY_START || kDown & KEY_B) {
				final = update_state(ROS_Exiting, 0);
			}
			break;
		case ROS_WaitingRebootExit:
			if (changing) {
				printf("\nPress X to reboot.");
				printf("\nPress START or B to exit.");
			}

			if (kDown & KEY_START || kDown & KEY_B) {
				final = update_state(ROS_Exiting, 0);
			} else if (kDown & KEY_X) {
				final = update_state(ROS_Rebooting, 0);
			}
			break;
		case ROS_CheckFiles:
			if (changing) {
				print_header();
				printf("Checking files.\n");

				if (!data.archives_mounted) {
					data.archives_mounted = mount_archives();
					if (!data.archives_mounted) {
						/*      -------------------------------------------------- */
						printf("Archives could not be mounted!\n");
						printf("This could be a permission issue.\n");
						final = update_state(ROS_ErrorWaitingExit, 0);
						break;
					}
				}

				data.secureinfo_ab_letter = 'A';
				if (!read_secureinfo('A', &data.secureinfo_ab)) {
					data.secureinfo_ab_letter = 'B';
					if (!read_secureinfo('B', &data.secureinfo_ab)) {
						/*      -------------------------------------------------- */
						printf("Could not read SecureInfo_A or SecureInfo_B.\n");
						printf("This should be impossible.\n");
						final = update_state(ROS_ErrorWaitingExit, 0);
						break;
					}
				}

				if (is_serial_number_valid(data.secureinfo_ab.serial)) {
					/*      -------------------------------------------------- */
					printf("SecureInfo_%c serial: %s\n", data.secureinfo_ab_letter, data.secureinfo_ab.serial);
					printf("\nSecureInfo_%c seems to already have a serial.\n", data.secureinfo_ab_letter);
					printf("You don't need this tool.\n");
					final = update_state(ROS_WaitingExit, 0);
					break;
				}

				data.secureinfo_c_loaded = read_secureinfo('C', &data.secureinfo_c);
				if (data.secureinfo_c_loaded) {
					printf("SecureInfo_C loaded, using region byte from that.\n");
					data.target_region = data.secureinfo_c.region;
				} else {
					printf("SecureInfo_C not loaded, using original file.\n");
					data.target_region = data.secureinfo_ab.region;
				}
				printf("Target region: %s\n", regions[data.target_region]);

				get_log_serial_number(data.log_serial_number);
				printf("inspect.log SN: %s\n", data.log_serial_number);

				// this doesn't need to worry about being null-terminated
				memcpy(data.secureinfo_ab_replacement.serial, data.log_serial_number, SERIAL_NUMBER_MAX - 1);
				data.secureinfo_ab_replacement.region = data.target_region;

				/*      -------------------------------------------------- */
				printf("\nThis tool will copy the above serial number to\n");
				printf("SecureInfo_%c. Do you want to continue?\n\n", data.secureinfo_ab_letter);

				printf("Press X to restore SecureInfo_%C.\n", data.secureinfo_ab_letter);
				printf("Press START or B to exit.");
			}

			if (kDown & KEY_START || kDown & KEY_B) {
				final = update_state(ROS_Exiting, 0);
			} else if (kDown & KEY_X) {
				final = update_state(ROS_RestoreFiles, 0);
			}
			break;
		case ROS_RestoreFiles:
			if (changing) {
				print_header();
				printf("Backing up old SecureInfo_* files.\n");
				if (!backup_old_secureinfo()) {
					final = update_state(ROS_ErrorWaitingExit, 0);
					break;
				}

				printf("Writing new SecureInfo_%c.\n", data.secureinfo_ab_letter);
				write_new_secureinfo_ab();

				/*      -------------------------------------------------- */
				printf("Success! Reboot so the new SecureInfo_%c loads.\n", data.secureinfo_ab_letter);
				
				final = update_state(ROS_WaitingRebootExit, 0);
			}

			if (kDown & KEY_START || kDown & KEY_B) {
				final = update_state(ROS_Exiting, 0);
			}
			break;
		case ROS_Rebooting:
			if (changing) {
				printf("Rebooting!\n");
			}
			break;
		case ROS_Exiting:
			break;
	}
	data.state = final;
	return final;
}

int main(int argc, char* argv[])
{
	gfxInitDefault();
	consoleInit(GFX_TOP, &data.top);
	consoleInit(GFX_BOTTOM, &data.bottom);
	consoleSelect(&data.top);

	data.state = ROS_Started;
	update_state(data.state, 0);

	srand(time(NULL));
	data.randval = rand() % 1000;

	// Main loop
	while (aptMainLoop())
	{
		gspWaitForVBlank();
		gfxSwapBuffers();
		hidScanInput();

		update_state(data.state, hidKeysDown());
		if (data.state == ROS_Exiting) break;
		if (data.state == ROS_Rebooting) break;
	}

	unmount_archives();

	gfxExit();

	if (data.state == ROS_Rebooting) {
		APT_HardwareResetAsync();
		while (1);
	}
	return 0;
}
