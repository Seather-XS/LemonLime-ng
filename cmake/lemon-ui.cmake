# ==================================================================================
# Lemon UI
# ==================================================================================

set(LEMON_BASEDIR_UI ${CMAKE_SOURCE_DIR}/src)

aux_source_directory(${LEMON_BASEDIR_UI} LEMON_UI_SOURCES)

list(APPEND LEMON_UI_SOURCES ${LEMON_BASEDIR_UI}/component/exportutil/exportutil.cpp)
list(APPEND LEMON_UI_SOURCES ${LEMON_BASEDIR_UI}/component/exportutil/exportutil.h)

# 准考证模块（数据模型 / 内置模板 / 生成器 / 编辑窗口 / 选项卡）
foreach(LEMON_ADMISSION_FILE admissioncsv admissionnotes admissiontemplate admissionproject
        admissiongenerator admissionwidget csveditordialog columndialog notesdialog admissionnaming
        admissioncolumns admissionassign idruledialog seatdialog venuedialog)
    list(APPEND LEMON_UI_SOURCES ${LEMON_BASEDIR_UI}/admission/${LEMON_ADMISSION_FILE}.cpp)
    list(APPEND LEMON_UI_SOURCES ${LEMON_BASEDIR_UI}/admission/${LEMON_ADMISSION_FILE}.h)
endforeach()

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
