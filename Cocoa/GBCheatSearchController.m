#import "GBCheatSearchController.h"
#import "GBWarningPopover.h"
#import "GBCheatWindowController.h"
#import "GBPanel.h"

@interface GBCheatSearchController() <NSTableViewDelegate, NSTableViewDataSource>
@property IBOutlet NSPopUpButton *dataTypeButton;
@property IBOutlet NSPopUpButton *conditionTypeButton;
@property IBOutlet NSTextField *operandField;
@property IBOutlet NSTextField *conditionField;
@property IBOutlet NSTextField *resultsLabel;
@property (strong) IBOutlet NSButton *addCheatButton;
@end

@implementation GBCheatSearchController
{
    __weak Document *_document;
    size_t _resultCount;
    GB_cheat_search_result_t *_results;
    GBPanel *_window;
}

+ (instancetype)controllerWithDocument:(Document *)document
{
    GBCheatSearchController *ret = [[self alloc] init];
    ret->_document = document;
    NSArray *objects;
    [[NSBundle mainBundle] loadNibNamed:@"CheatSearch" owner:ret topLevelObjects:&objects];
    ret->_resultsLabel.stringValue = @"";
    ret->_resultsLabel.cell.backgroundStyle = NSBackgroundStyleRaised;
    ret->_window.ownerWindow = document.mainWindow;
    return ret;
}

- (IBAction)reset:(id)sender
{
    _dataTypeButton.enabled = true;
    [_document performAtomicBlock:^{
        GB_cheat_search_reset(_document.gb);
    }];
    _resultCount = 0;
    if (_results) {
        free(_results);
        _results = NULL;
    }
    [_tableView reloadData];
    _resultsLabel.stringValue = @"";
}

- (IBAction)search:(id)sender
{
    // Dispatch to work around firstResponder oddities
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([sender isKindOfClass:[NSTextField class]]) {
            // Action sent by losing focus rather than pressing enter
            if (![sender currentEditor]) return;
        }
        _dataTypeButton.enabled = false;
        [_document performAtomicBlock:^{
            __block bool success = false;
            NSString *error = [_document captureOutputForBlock:^{
                success = GB_cheat_search_filter(_document.gb, _conditionField.stringValue.UTF8String, _dataTypeButton.selectedTag);
            }];
            if (!success) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [GBWarningPopover popoverWithContents:error onView:_conditionField];
                    NSBeep();
                });
                return;
            }
            _resultCount = GB_cheat_search_result_count(_document.gb);
            _results = malloc(sizeof(*_results) * _resultCount);
            GB_cheat_search_get_results(_document.gb, _results);
        }];
        if (_resultCount == 0) {
            _dataTypeButton.enabled = true;
            _resultsLabel.stringValue = @"No results.";
        }
        else {
            _resultsLabel.stringValue = [NSString stringWithFormat:@"%@ result%s",
                                         [NSNumberFormatter localizedStringFromNumber:@(_resultCount)
                                                                          numberStyle:NSNumberFormatterDecimalStyle],
                                         _resultCount > 1? "s" : ""];
        }
        [_tableView reloadData];
    });
}

- (IBAction)conditionChanged:(id)sender
{
    unsigned index = [_conditionTypeButton indexOfSelectedItem];
    _conditionField.enabled = index == 11;
    _operandField.enabled = index >= 1 && index <= 6;
    switch ([_conditionTypeButton indexOfSelectedItem]) {
        case 0: _conditionField.stringValue = @"1"; break;
        case 1: _conditionField.stringValue = [NSString stringWithFormat:@"new == (%@)", _operandField.stringValue]; break;
        case 2: _conditionField.stringValue = [NSString stringWithFormat:@"new != (%@)", _operandField.stringValue]; break;
        case 3: _conditionField.stringValue = [NSString stringWithFormat:@"new > (%@)", _operandField.stringValue]; break;
        case 4: _conditionField.stringValue = [NSString stringWithFormat:@"new >= (%@)", _operandField.stringValue]; break;
        case 5: _conditionField.stringValue = [NSString stringWithFormat:@"new < (%@)", _operandField.stringValue]; break;
        case 6: _conditionField.stringValue = [NSString stringWithFormat:@"new <= (%@)", _operandField.stringValue]; break;
        case 7: _conditionField.stringValue = @"new != old"; break;
        case 8: _conditionField.stringValue = @"new == old"; break;
        case 9: _conditionField.stringValue = @"new > old"; break;
        case 10: _conditionField.stringValue = @"new < old"; break;
    }
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
    return _resultCount;
}

- (uint8_t *)addressForRow:(unsigned)row
{
    uint8_t *base;
    uint32_t offset;
    if (_results[row].addr < 0xc000) {
        base = GB_get_direct_access(_document.gb, GB_DIRECT_ACCESS_CART_RAM, NULL, NULL);
        offset = (_results[row].addr & 0x1FFF) + _results[row].bank * 0x2000;
    }
    else if (_results[row].addr < 0xe000) {
        base = GB_get_direct_access(_document.gb, GB_DIRECT_ACCESS_RAM, NULL, NULL);
        offset = (_results[row].addr & 0xFFF) + _results[row].bank * 0x1000;
    }
    else {
        base = GB_get_direct_access(_document.gb, GB_DIRECT_ACCESS_HRAM, NULL, NULL);
        offset = (_results[row].addr & 0x7F);
    }
    return base + offset;
}

- (id)tableView:(NSTableView *)tableView objectValueForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    switch ([[tableView tableColumns] indexOfObject:tableColumn]) {
        case 0:
            return [NSString stringWithFormat:@"$%02x:$%04x", _results[row].bank, _results[row].addr];
        case 1:
            if (_dataTypeButton.selectedTag & GB_CHEAT_SEARCH_DATA_TYPE_16BIT) {
                return [NSString stringWithFormat:@"$%04x", _results[row].value];
            }
            return [NSString stringWithFormat:@"$%02x", _results[row].value];
        default: {
            const uint8_t *data = [self addressForRow:row];
            GB_cheat_search_data_type_t dataType = _dataTypeButton.selectedTag;
            uint16_t value = data[0];
            if (!(dataType & GB_CHEAT_SEARCH_DATA_TYPE_16BIT)) {
                return [NSString stringWithFormat:@"$%02x", value];
            }
            value |= data[1] << 8;
            if ((dataType & GB_CHEAT_SEARCH_DATA_TYPE_BE_BIT)) {
                value = __builtin_bswap16(value);
            }
            return [NSString stringWithFormat:@"$%04x", value];
        }
    }
}

- (void)tableView:(NSTableView *)tableView setObjectValue:(NSString *)object forTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    [_document performAtomicBlock:^{
        __block bool success = false;
        __block uint16_t value;
        NSString *error = [_document captureOutputForBlock:^{
            success = !GB_debugger_evaluate(_document.gb, object.UTF8String, &value, NULL);
        }];
        if (!success) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [GBWarningPopover popoverWithContents:error onView:tableView];
                NSBeep();
            });
            return;
        }
        uint8_t *dest = [self addressForRow:row];
        GB_cheat_search_data_type_t dataType = _dataTypeButton.selectedTag;
        if (dataType & GB_CHEAT_SEARCH_DATA_TYPE_BE_BIT) {
            value = __builtin_bswap16(value);
        }
        dest[0] = value;
        if (dataType & GB_CHEAT_SEARCH_DATA_TYPE_16BIT) {
            dest[1] = value >> 8;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [tableView reloadData];
        });
    }];
}

- (void)controlTextDidChange:(NSNotification *)obj
{
    [self conditionChanged:nil];
}

- (IBAction)addCheat:(id)sender
{
    GB_cheat_search_result_t *result = _results + _tableView.selectedRow;
    uint8_t *data = [self addressForRow:_tableView.selectedRow];
    GB_cheat_search_data_type_t dataType = _dataTypeButton.selectedTag;
    size_t rowToSelect = 0;
    GB_get_cheats(_document.gb, &rowToSelect);
    [_document performAtomicBlock:^{
        GB_add_cheat(_document.gb,
                     (dataType & GB_CHEAT_SEARCH_DATA_TYPE_16BIT)? "New Cheat (Part 1)" : "New Cheat",
                     result->addr, result->bank,
                     *data,
                     0, false,
                     true);
        if (dataType & GB_CHEAT_SEARCH_DATA_TYPE_16BIT) {
            GB_add_cheat(_document.gb,
                         (dataType & GB_CHEAT_SEARCH_DATA_TYPE_16BIT)? "New Cheat (Part 2)" : "New Cheat",
                         result->addr + 1, result->bank,
                         data[1],
                         0, false,
                         true);
        }
        GB_set_cheats_enabled(_document.gb, true);
    }];
    [_document.cheatsWindow makeKeyAndOrderFront:nil];
    [_document.cheatWindowController.cheatsTable reloadData];
    [_document.cheatWindowController.cheatsTable selectRow:rowToSelect byExtendingSelection:false];
    [_document.cheatWindowController.cheatsTable.delegate tableViewSelectionDidChange:nil];
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
    _addCheatButton.enabled = _tableView.numberOfSelectedRows != 0;
}

- (void)dealloc
{
    if (_results) free(_results);
}

@end
