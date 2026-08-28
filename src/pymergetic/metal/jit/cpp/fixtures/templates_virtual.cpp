/* cppx templates+virtual fixture: template class with inheritance, virtual
 * methods, pure virtual, template member, using declarations. */
template <typename T>
class Box {
public:
    Box(T value) : value_(value) {}
    virtual ~Box() {}

    T get() const {
        return value_;
    }

    virtual int describe() const = 0;

private:
    T value_;
};

class LoudBox : public Box<int> {
public:
    using Box<int>::get;

    LoudBox(int v) : Box<int>(v) {}
    int describe() const override {
        return 2;
    }
};

template <typename T>
T identity(T x) {
    return x;
}

int use() {
    LoudBox box(7);
    std::unique_ptr<LoudBox> p(new LoudBox(8));
    return box.describe() + identity<int>(3) + p->describe();
}
