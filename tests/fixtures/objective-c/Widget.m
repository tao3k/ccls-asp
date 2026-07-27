@protocol Readable
- (int)readValue;
@end

@interface Entity
@end

@interface Widget : Entity <Readable>
@property int value;
- (int)readValue;
@end

@implementation Widget
- (int)readValue {
  return self.value;
}
@end
