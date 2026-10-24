# ==================================================================================
# Lemon UI
# ==================================================================================

set(LEMON_BASEDIR_UI ${CMAKE_SOURCE_DIR}/src)

aux_source_directory(${LEMON_BASEDIR_UI} LEMON_UI_SOURCES)

list(APPEND LEMON_UI_SOURCES ${LEMON_BASEDIR_UI}/component/exportutil/exportutil.cpp)
list(APPEND LEMON_UI_SOURCES ${LEMON_BASEDIR_UI}/component/exportutil/exportutil.h)

# 准考证（${LEMON_BASEDIR_UI}/admission/）暂时不编进 lemon.exe：源码与模板都留在仓库里，
# 需要时把下面这段加回来，并恢复 lemon.ui 的 Admission 选项卡、lemon.cpp 里的接线。
# foreach(LEMON_ADMISSION_FILE admissioncsv admissionnotes admissiontemplate admissionproject
#         admissiongenerator admissionwidget csveditordialog columndialog notesdialog admissionnaming
#         admissioncolumns admissionassign idruledialog seatdialog venuedialog)
#     list(APPEND LEMON_UI_SOURCES ${LEMON_BASEDIR_UI}/admission/${LEMON_ADMISSION_FILE}.cpp)
#     list(APPEND LEMON_UI_SOURCES ${LEMON_BASEDIR_UI}/admission/${LEMON_ADMISSION_FILE}.h)
# endforeach()

set(LEMON_UI_FORMS
    ${LEMON_BASEDIR_UI}/forms/lemon.ui
    ${LEMON_BASEDIR_UI}/forms/daydialog.ui
    ${LEMON_BASEDIR_UI}/forms/exttestcasemodifierdialog.ui
    ${LEMON_BASEDIR_UI}/forms/exttestcasemodifier.ui
    ${LEMON_BASEDIR_UI}/forms/exttestcaseupdaterdialog.ui
    ${LEMON_BASEDIR_UI}/forms/taskeditwidget.ui
    ${LEMON_BASEDIR_UI}/forms/testcaseeditwidget.ui
    ${LEMON_BASEDIR_UI}/forms/generalsettings.ui
    ${LEMON_BASEDIR_UI}/forms/compilersettings.ui
    ${LEMON_BASEDIR_UI}/forms/addtestcaseswizard.ui
    ${LEMON_BASEDIR_UI}/forms/judgingdialog.ui
    ${LEMON_BASEDIR_UI}/forms/optionsdialog.ui
    ${LEMON_BASEDIR_UI}/forms/detaildialog.ui
    ${LEMON_BASEDIR_UI}/forms/newcontestwidget.ui
    ${LEMON_BASEDIR_UI}/forms/opencontestwidget.ui
    ${LEMON_BASEDIR_UI}/forms/newcontestdialog.ui
    ${LEMON_BASEDIR_UI}/forms/opencontestdialog.ui
    ${LEMON_BASEDIR_UI}/forms/welcomedialog.ui
    ${LEMON_BASEDIR_UI}/forms/addtaskdialog.ui
    ${LEMON_BASEDIR_UI}/forms/advancedcompilersettingsdialog.ui
    ${LEMON_BASEDIR_UI}/forms/environmentvariablesdialog.ui
    ${LEMON_BASEDIR_UI}/forms/editvariabledialog.ui
    ${LEMON_BASEDIR_UI}/forms/addcompilerwizard.ui
    ${LEMON_BASEDIR_UI}/forms/statisticsbrowser.ui
    )
