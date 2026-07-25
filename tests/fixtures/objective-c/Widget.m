@interface Widget
@property int value;
- (int)readValue;
@end

@implementation Widget
- (int)readValue {
  return self.value;
}
@end
