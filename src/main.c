#include "minios/system.h"

#include <stdio.h>

/*
 * miniOS 통합 실행 진입점
 * - apps/의 LAB별 demo가 기본 관찰 지점이다.
 * - 이 파일은 학생이 최종 통합 실험을 할 때 필요한 최소 main만 제공한다.
 */

int main(void) {
    mos_kernel_t kernel;
    mos_system_report_t report;
    mos_status_t status;

    status = mos_system_boot(&kernel, &report);
    printf("LAB10 system boot status: %d\n", status);
    printf("LAB10 labs ready after boot: %u\n", report.labs_ready);
    if (status != MOS_OK) {
        return 1;
    }

    status = mos_system_run_demo(&kernel, &report);
    printf("LAB10 integration demo status: %d\n", status);
    printf("LAB10 labs ready after demo: %u\n", report.labs_ready);
    (void)mos_kernel_shutdown(&kernel);
    return status == MOS_OK ? 0 : 1;
}
